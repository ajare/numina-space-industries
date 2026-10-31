#pragma once
namespace serialization { class Serializer; }
#include "station/StationHistory.h"
#include "station/Persistence.h"
#include "station/StationLiveNodes.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace station
{
	using serialization::Serializer;
	// Test-runner-only override used to execute an existing Station scenario
	// under one requested ratio without rewriting its authored fixture.
	void setStationSystemsRuntimeStepRatioOverrideForTesting(std::optional<uint32_t> ratio);
	std::optional<uint32_t> stationSystemsRuntimeStepRatioOverrideForTesting();

	// Fixed-ratio comparisons temporarily take ownership of the test clock.
	// Restore the caller's selected ratio even when an assertion throws.
	class ScopedStationStepRatioForTesting
	{
	public:
		explicit ScopedStationStepRatioForTesting(std::optional<uint32_t> ratio)
			: previous(stationSystemsRuntimeStepRatioOverrideForTesting())
		{ setStationSystemsRuntimeStepRatioOverrideForTesting(ratio); }
		~ScopedStationStepRatioForTesting()
		{ setStationSystemsRuntimeStepRatioOverrideForTesting(previous); }
		ScopedStationStepRatioForTesting(ScopedStationStepRatioForTesting const&) = delete;
		ScopedStationStepRatioForTesting& operator=(ScopedStationStepRatioForTesting const&) = delete;
	private:
		std::optional<uint32_t> previous;
	};

	enum class StationSystemsUpdateMode { Fixed, Variable };

	// Station drive. The coordinator supplies target seconds in a World;
	// standalone Station scenarios may drive the same boundary directly.
	class StationSystemsRuntime
	{
	public:
		StationSystemsRuntime(StationSystemsSettings settings, float baseStepSeconds,
			StationSystemsSource source = std::nullopt);
		// Variable mode has no World quantum. Each update supplies its own duration.
		StationSystemsRuntime(StationSystemsSettings settings, StationSystemsUpdateMode mode,
			StationSystemsSource source = std::nullopt);
		StationSystemsUpdateMode updateMode() const;
		// Exactly one variable-duration Station update (internal event-driven
		// subdivisions remain enabled). Positive finite seconds only; invalid
		// or unrepresentable durations and fixed-mode calls are refused unchanged.
		// Disabled settings are a no-op. Failed steps retain committed time and
		// retire the run, just like advanceTo. Never mix the two drive modes.
		bool update(double deltaSeconds);
		// Station-wide direct stellar irradiance in W/m², already attenuated by
		// eclipse. Held until replaced; defaults to zero. Derived arrays only:
		// legacy arrays continue to use the authored solar cycle. Invalid
		// (negative or non-finite) values are refused without changing input.
		// Changes are journalled at the current committed step for replay;
		// identical values are no-ops and do not grow the journal.
		bool setDirectSolarIrradianceWPerM2(double irradiance);
		~StationSystemsRuntime();
		StationSystemsRuntime(StationSystemsRuntime const&) = delete;
		StationSystemsRuntime& operator=(StationSystemsRuntime const&) = delete;
		// Catch up using whole Station steps until elapsed >= target. Targets
		// already reached are no-ops; invalid targets are rejected without mutation.
		// Available only in Fixed mode.
		bool advanceTo(double simulatedSeconds);
		// Mean completed Station step cost in the trailing wall-clock second, or
		// zero when no Station step completed. Sampled per Station step, so a
		// sparse cadence (the default is one step per six World ticks) does not
		// dilute it: the World ticks that skip a Station step add no sample.
		// Diagnostic only; never affects simulation state or time.
		double averageStepMicroseconds() const;
		// Apply a changed configuration to a running or paused scenario
		// without resetting the World. Topology is frozen and enforced: the
		// entity id set of every collection, atmosphere pathway, Thermal link
		// and Heat engine endpoints, and connection map keys must match the running
		// configuration exactly, or nothing is applied and a diagnostic
		// explains the refusal. Parameters (areas, capacities, rates, limits,
		// thermal properties, priorities) take effect from the next step.
		// Live accumulated state - inventories, temperatures, current
		// openness, net totals, fired flags, trend history - is preserved.
		// Initial-condition fields (startingQualified, startingTemperatureC,
		// startingTemperatureC, startingEnergyKJ and friends) are NOT applied:
		// the conservation audit derives its baseline from them at snapshot
		// time, so changing one mid-run would shift that baseline
		// retroactively. Changing a starting value is a reset, not a live edit.
		// Monitor rules are validated at this seam as well as at the World
		// facade. Enable/disable and rule edits reset pending persistence;
		// disabling closes active Alerts at current Station time.
		bool updateSettings(StationSystemsSettings const& offered,
			std::string* diagnostic = nullptr);
		// The configuration the runtime is actually executing, including the
		// initial-condition fields it preserved against a live update. Callers
		// that persist settings should store this rather than what they sent,
		// so a saved document agrees with the running scenario.
		StationSystemsSettings const& effectiveSettings() const;
		// Change one node's parameters in place, without the caller owning a
		// copy of the configuration and without re-validating the document.
		// The Settings type selects which collection to edit and the id
		// selects the node, so the mutator is type-checked at the call site
		// and no field-name strings are involved. The callback receives a scoped
		// candidate; it must not retain references or re-enter this runtime.
		// Callback exceptions propagate without applying the candidate:
		//
		//   runtime.updateNode<AtmospherePathwaySettings>(
		//       "hatch", [](auto& hatch) { hatch.areaM2 = 25.0; });
		//
		// The edit looks up and copies one entry, mirrors it into the settings
		// copy, and re-sorts that collection's priority ordering if it has one.
		// Accepted changes also journal the effective configuration for replay.
		//
		// Topology is untouched by construction: only an existing entry's
		// parameters change, so no id, endpoint or connection can move. A
		// mutator that touched an initial-condition field is refused whole,
		// before anything is written, rather than half-applied.
		template <class Settings, class Mutator>
		bool updateNode(std::string_view nodeId, Mutator&& mutate,
			std::string* diagnostic = nullptr)
		{
			size_t index = SIZE_MAX;
			Settings* entry = liveAuthoredEntry<Settings>(nodeId, &index);
			if (!entry)
			{
				if (diagnostic)
					*diagnostic = std::string(LiveNode<Settings>::nodeType) + " '"
						+ std::string(nodeId) + "' is not in the running scenario";
				return false;
			}
			Settings const before = *entry;
			// Mutate a detached candidate: callbacks never borrow live state, and
			// an exception cannot leave an unjournalled partial edit behind.
			Settings candidate = before;
			std::forward<Mutator>(mutate)(candidate);
			if (candidate.id != before.id)
			{
				if (diagnostic) *diagnostic = "Node identity is frozen topology and cannot be changed live";
				return false;
			}
			if constexpr (requires (std::string& reason) { LiveNode<Settings>::validateParameters(candidate, reason); })
			{
				std::string reason;
				if (!LiveNode<Settings>::validateParameters(candidate, reason))
				{
					if (diagnostic) *diagnostic = std::move(reason);
					return false;
				}
			}
			if constexpr (requires { LiveNode<Settings>::topologyChanged(before, candidate); })
			{
				if (LiveNode<Settings>::topologyChanged(before, candidate))
				{
					if (diagnostic)
						*diagnostic = std::string(LiveNode<Settings>::nodeType) + " '"
							+ std::string(nodeId) + "': identity, endpoints, chemistry and fault/wear opt-ins are frozen topology and cannot be changed live";
					return false;
				}
			}
			// Refuse BEFORE writing anything further, so a mutator that mixed a
			// live field with a preserved one cannot have the live half land.
			if (LiveNode<Settings>::restoreInitial(before, candidate))
			{
				if (diagnostic)
					*diagnostic = std::string(LiveNode<Settings>::nodeType) + " '"
						+ std::string(nodeId)
						+ "': initial-condition fields cannot be changed live, so the whole edit was refused";
				return false;
			}
			if (candidate == before)
			{
				if (diagnostic) diagnostic->clear();
				return true;
			}
			// Keep the settings copy and the priority ordering in step with the
			// live entry we just changed. Both are scoped to this collection.
			*entry = std::move(candidate);
			syncLiveNodeMirror<Settings>(index, *entry);
			resortLiveNode<Settings>();
			if (diagnostic) diagnostic->clear();
			return true;
		}
		// Overload that also names the node type, so a caller holding a runtime
		// node type string can check it agrees with the settings type it edits.
		template <class Settings, class Mutator>
		bool updateNode(std::string_view nodeType, std::string_view nodeId,
			Mutator&& mutate, std::string* diagnostic = nullptr)
		{
			if (nodeType != LiveNode<Settings>::nodeType)
			{
				if (diagnostic)
					*diagnostic = "node type '" + std::string(nodeType)
						+ "' is not edited by " + std::string(nodeId) + "'s settings type";
				return false;
			}
			return updateNode<Settings>(nodeId, std::forward<Mutator>(mutate), diagnostic);
		}
		// Validated whole, queued for the next Station step. Cause is client
		// attribution, never an authority or permission check.
		bool injectFault(std::string_view installationId, std::string_view modeId,
			FaultCause const& cause, std::string* diagnostic = nullptr);
		// Free, immediate latch clear. Refuses pending/unlatched Trips and
		// non-Trip Faults. An unresolved trigger may latch again next step.
		bool resetTrip(std::string_view installationId, std::string* diagnostic = nullptr);
		bool breakerReset(std::string_view busId, std::string* diagnostic = nullptr);
		// Funded Condition recovery (#840): heal an Installation toward
		// targetCondition, paying for every restored point from the
		// service-stock store its Condition model names, above that
		// store's protected reserve, at the authored per-hour bound.
		// Never instant and never free: refused whole when the
		// Installation has no Condition model, the target is not above
		// the current Condition, no funding store is declared, or the
		// stock above the reserve is already gone.
		bool restoreCondition(std::string_view installationId, double targetCondition,
			std::string* diagnostic = nullptr);
		// #842: start a repair as a runtime-instantiated #414 maintenance
		// job. Same isolation-at-zero while running, same exclusive
		// reservation of authored parts (drawn from the Installation's
		// repair-parts store per the fault mode's repair plan), same
		// full-duration completion and interruption-discards-progress
		// semantics as scheduled maintenance. Completion clears the
		// target Fault and books the consumed parts into the plan's waste
		// store as qualified, disposal-ready material; Repair never
		// reverses Wear. The accepted command returns the job id. It is
		// refused whole, with a diagnostic and no mutation, on an unknown
		// installation or fault mode, a mode with no repair plan, a target
		// Fault that is not present, a repair already running for that
		// Fault, or parts that cannot currently be reserved.
		bool repairFault(std::string_view installationId, std::string_view faultModeId,
			std::string* jobIdOut = nullptr, std::string* diagnostic = nullptr);
		// Command journal plus the committed clock/duration journal reconstructs the fault ledger and
		// all its bookings through the normal audited drive (YAML or binary).
		void writeFaultReplay(Serializer& serializer) const;
		static std::unique_ptr<StationSystemsRuntime> readFaultReplay(Serializer& serializer,
			StationSystemsSettings const& settings, float baseStepSeconds,
			std::uint32_t settingsSchemaVersion = kStationSchemaVersion,
			StationSystemsSource source = std::nullopt);
		bool hasFaultCommands() const;
		// Whether a save must carry the fault/Condition replay block.
		// Always true in Variable mode, which must preserve its duration journal.
		// Condition is integrated over the run (#840), so a save of a
		// Condition-modelled machine needs the block even when no client
		// ever issued a command: without it the machine would come back
		// healthy and unworn.
		bool hasFaultReplayState() const;
		// Read-only reporting of the committed run; observation never advances
		// time or actuates equipment. Snapshot/history exports are owned copies.
		// Snapshot totals remain accumulated values; last-step fields retain
		// their declared units/step meaning (see StationSystems.h). Chart metrics
		// use the canonical units/semantics in StationMetricDescriptor instead.
		StationSystemsSnapshot snapshot() const;
		// Borrowed newest-N event window (StationEventHistoryLimit). Copy before
		// advancing or editing the runtime; those operations can invalidate it.
		std::vector<StationSystemsEvent> const& events() const;
		std::vector<StationTrendSeries> trends() const;
		std::vector<StationMetricHistory> metrics() const;
		// Recording can be explicitly limited/refused without disabling the
		// simulation. Never interpret an empty history as a measured zero.
		StationRecordingStatus recordingStatus() const;
		// Run identity plus retained-history revision, not a physical-state
		// revision. Unchanged reads/reached-target advances leave this intact.
		StationObservationRevision observationRevision() const;
		// Borrowed catalogue for this applied run, including expected members.
		std::vector<StationChartDefinition> const& catalogue() const;
		// Borrowed single-entry cache: another query can replace its contents.
		// Copy before querying a different id or mutating the runtime. Repeated
		// id/history-revision queries reuse preparation; unknown ids are empty.
		StationChartData const& query(std::string const& id) const;
		StationQueryWork queryWork() const;
		std::vector<StationSystemsDiagnostic> const& diagnostics() const;
	private:
		StationSystemsRuntime(StationSystemsSettings settings, float baseStepSeconds,
			StationSystemsSource source, StationSystemsUpdateMode mode);
		// Implemented in StationSystemsLiveSettings.cpp, where the private
		// Impl state is visible, and explicitly instantiated there for every
		// live-editable settings type. The mutator lambda never crosses into
		// that translation unit, so no type erasure is needed.
		template <class Settings>
		Settings* liveAuthoredEntry(std::string_view nodeId, size_t* index);
		template <class Settings>
		void syncLiveNodeMirror(size_t index, Settings const& value);
		template <class Settings>
		void resortLiveNode();
		bool advanceStep();
		struct Impl;
		std::unique_ptr<Impl> mImpl;
		uint64_t mRun{0};
	};
}
