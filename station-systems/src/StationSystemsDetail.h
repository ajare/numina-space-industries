#pragma once
#include "station/StationSystemsRuntime.h"
#include "CompositionDetail.h"
#include "station/StationConnectionLedger.h"
#include "station/StationMaterialConnections.h"
#include "serialization/SerializationException.h"
#include "station/support/ScriptSandbox.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <numeric>
#include <queue>
#include <set>


namespace station::station_detail
{

	constexpr size_t EventLimit = StationEventHistoryLimit;
	constexpr size_t DiagnosticByteLimit = 2048;
	// #418: the per-series sample budget for bounded trends. Storage
	// never exceeds this many samples per series no matter how long or
	// how fast the World runs; overflow coarsens the history instead
	// of growing it.
	constexpr size_t TrendSampleLimit = StationTrendSampleLimit;
	// Fuel energy is authored in the store's unit; the generator's
	// physics runs in kWh. One factor converts store units to kWh so
	// host and sandbox agree on every draw (#399). The scenario
	// forecast reads the same contract to project generator fuel draw
	// in the store's own unit (#594).
	inline double stationEnergyUnitsToKWh(std::string const& unit)
	{
		if (unit == "kJ") return 1.0 / 3600.0;
		if (unit == "MJ") return 1000.0 / 3600.0;
		if (unit == "Wh") return 1.0 / 1000.0;
		return 1.0;
	}
	// Every public event producer appends through this one bounded
	// helper (#551): the history never retains more than the newest
	// EventLimit entries, evicting oldest-first, so a tick that
	// announces several transitions at once cannot push the history
	// past the bound, and later appends keep evicting no matter how
	// the vector grew. Declaration order within a tick is preserved.
	// The category (#418) names the subsystem family on the stable
	// presentation contract so incidents render without message parsing.
	inline void appendBoundedEvent(std::vector<StationSystemsEvent>& events,
		uint64_t tick, std::string category, std::string message)
	{
		while (events.size() >= EventLimit) events.erase(events.begin());
		events.push_back({tick, std::move(message), std::move(category)});
	}
	// #418/#603: one bounded trend observation with cadence-aware
	// decimation. A series at downsampleLevel L records one sample
	// every 2^L base observations: consecutive samples always stand
	// exactly sampleIntervalSeconds = base x 2^L of simulation time
	// apart, so every sample in the series covers the same amount of
	// time and the pairwise merge average stays time-weighted-correct
	// - already-merged samples and newly recorded ones carry equal
	// weight because they carry equal spans. Observations that fall
	// inside the current stride are not recorded (the live value
	// stays visible in the snapshot); the history skips them instead
	// of appending raw fine-cadence samples onto a coarsened series
	// the way the old merge-then-append-at-base-rate scheme did,
	// which made the declared stride fictitious and averaged unequal
	// time spans against each other (#603). Coarsening halves the
	// count and doubles the stride on every overflow, so storage
	// never exceeds TrendSampleLimit; the level is capped at
	// StationTrendDownsampleLevelCap, past which the window slides
	// oldest-first instead of doubling again, keeping the stride and
	// every 2^level shift finite for any run length. Samples older
	// than the authored retention are dropped outright. Both
	// operations mark the series truncated: the presentation shows
	// the gap rather than imply complete history. Deterministic and
	// read-only with respect to the simulated state - trends never
	// feed back into the ledger.
	inline void appendBoundedTrendSampleImpl(std::vector<StationTrendSeries>& series,
		std::string const& subsystem, std::string const& id,
		std::string const& unit, double baseIntervalSeconds,
		double retentionSeconds,
		uint64_t tick, double elapsedSeconds, double requested, double actual)
	{
		auto found = std::ranges::find(series, std::make_pair(subsystem, id),
			[](StationTrendSeries const& entry)
			{ return std::make_pair(entry.subsystem, entry.id); });
		if (found == series.end())
		{
			StationTrendSeries fresh;
			fresh.subsystem = subsystem;
			fresh.id = id;
			fresh.unit = unit;
			fresh.sampleIntervalSeconds = baseIntervalSeconds;
			series.push_back(std::move(fresh));
			found = prev(series.end());
		}
		auto& target = *found;
		if (target.samples.size() >= TrendSampleLimit)
		{
			if (target.downsampleLevel < StationTrendDownsampleLevelCap)
			{
				// Every sample stands for the same span, so the
				// pairwise mean is the correct time-weighted merge.
				std::vector<StationTrendSample> merged;
				merged.reserve(TrendSampleLimit / 2 + 1);
				size_t index = 0;
				for (; index + 1 < target.samples.size(); index += 2)
				{
					auto const& first = target.samples[index];
					auto const& second = target.samples[index + 1];
					merged.push_back({second.worldTick, second.elapsedSeconds,
						(first.requested + second.requested) / 2.0,
						(first.actual + second.actual) / 2.0});
				}
				if (index < target.samples.size()) merged.push_back(target.samples[index]);
				target.samples = std::move(merged);
				++target.downsampleLevel;
				// Exact in binary floating point: scaling by a power
				// of two never rounds, so the stride stays the true
				// cadence instead of a drifting product.
				target.sampleIntervalSeconds = std::ldexp(baseIntervalSeconds,
					static_cast<int>(target.downsampleLevel));
				target.truncated = true;
			}
			else
			{
				// At the cap the stride never doubles again: the
				// window slides oldest-first so the count stays
				// bounded and the published stride and every shift of
				// the level stay finite forever.
				while (target.samples.size() >= TrendSampleLimit)
				{
					target.samples.erase(target.samples.begin());
					target.truncated = true;
				}
			}
		}
		// Cadence-aware decimation: with the coarsening above already
		// applied, the series' current stride is the minimum spacing
		// a new sample may have. An observation inside it is skipped
		// rather than appended, so the uniform-spacing invariant and
		// the equal-span averaging survive every coarsening.
		auto const level = std::min(target.downsampleLevel, StationTrendDownsampleLevelCap);
		auto const stride = uint64_t{1} << level;
		// Compare whole observation spans, not World ticks. The caller supplies
		// the effective cadence in systems seconds; round away float-grid noise.
		if (!target.samples.empty()
			&& std::round((elapsedSeconds - target.samples.back().elapsedSeconds) / baseIntervalSeconds)
				< static_cast<double>(stride))
			return;
		target.samples.push_back({tick, elapsedSeconds, requested, actual});
		while (target.samples.size() > 1
			&& elapsedSeconds - target.samples.front().elapsedSeconds > retentionSeconds)
		{
			target.samples.erase(target.samples.begin());
			target.truncated = true;
		}
	}
	// Borrow the Lua error only long enough to copy a bounded string. Never
	// coerce arbitrary error objects outside a protected allocation boundary.
	inline std::string errorMessage(lua_State* state, char const* fallback)
	{
		if (lua_type(state, -1) != LUA_TSTRING) return fallback;
		size_t length = 0;
		auto const* text = lua_tolstring(state, -1, &length);
		return std::string(text, std::min(length, DiagnosticByteLimit));
	}
	struct ScriptObject
	{
		// The station lifecycle model grows with each systems slice;
		// the sandbox budget is sized to the compiled footprint of the
		// bundled model plus headroom, not to a scenario cost - the
		// per-tick cost follows the configured scenario, and compile
		// footprint is what the construction pass allocates. The algae
		// culture slice (#411) pushed the compiled model past the
		// previous 512 KiB ceiling; the living biological constituent
		// inventories (#571), which the sandbox mirrors tick for tick
		// alongside the host ledger, pushed it past 1 MiB. The budget
		// rises with the model as the crop and algae slices did before
		// it.
		script::ScratchBudget budget{2 * 1024 * 1024, 250000};
		script::ModuleLoader loader;
		std::unique_ptr<lua_State, script::StateCloser> state;
		int reference{LUA_NOREF};
		ScriptObject() : state(lua_newstate(script::budgetedAllocate, &budget))
		{
			if (!state) throw serialization::SerializationException("Cannot allocate Station systems sandbox");
			lua_atpanic(state.get(), script::luaPanic);
			std::string diagnostic;
			if (script::runScratchSetup(state.get(), &loader, nullptr, diagnostic) != LUA_OK)
				throw serialization::SerializationException(diagnostic);
			lua_settop(state.get(), 0);
		}
	};
	using namespace station::configuration_detail;
}
