#include "station/Configuration.h"
#include <algorithm>
#include <cmath>
namespace station
{
	double evaluateStationProfile(ProfileSettings const& profile, double elapsedSeconds)
	{
		if (profile.periodSeconds <= 0.0) return profile.constantLevel;
		auto const phase = std::fmod(std::max(0.0, elapsedSeconds), profile.periodSeconds);
		double level = profile.segments.front().level;
		for (auto const& segment : profile.segments)
		{
			if (segment.startSeconds > phase) break;
			level = segment.level;
		}
		return level;
	}
	double stationAtmospherePartialPressureKPa(double massKg,
		double molarMassKgPerMol, double volumeM3, double temperatureC)
	{
		// p = (m / M) x R x T / V, converted from Pa to kPa. The ideal
		// gas assumption is the documented model: every compartment
		// pressure derives from this one formula, so an analytic mixture
		// check can verify it directly.
		return massKg / molarMassKgPerMol * StationUniversalGasConstantJPerMolK
			* (temperatureC + StationCelsiusOffset) / volumeM3 / 1000.0;
	}
	double stationSaturationVapourPressureKPa(double temperatureC)
	{
		// Magnus form over liquid water: the documented vapour-capacity
		// assumption for compartment humidity: p_sat = 0.61094 x
		// exp(17.625 t / (t + 243.04)). The expression is singular at
		// -243.04 C and its exponent overflows to infinity below that
		// point, so the documented supported domain is strictly above
		// the singular temperature. At or below it the model carries a
		// finite valid continuation instead of the raw expression: air
		// that cold holds no vapour at all, which is exactly where the
		// in-domain curve is already heading, so the continuation is a
		// vapour capacity of zero rather than an overflow published as
		// a shortage (#565).
		if (temperatureC <= -243.04) return 0.0;
		return 0.61094 * std::exp(17.625 * temperatureC / (temperatureC + 243.04));
	}
	double stationSaturatedVapourMassKg(double saturationKPa,
		double volumeM3, double temperatureC)
	{
		// The inverse of the ideal-gas partial pressure at saturation:
		// the water vapour mass a compartment of this volume holds at
		// this temperature before the air cannot hold more.
		return saturationKPa * 1000.0 * volumeM3 * StationMolarMassWaterKgPerMol
			/ (StationUniversalGasConstantJPerMolK * (temperatureC + StationCelsiusOffset));
	}
	uint64_t stationScheduleOccurrencesDue(double firstOccurrenceSeconds,
		double recurrenceSeconds, double elapsedSeconds)
	{
		// Nothing is due before the first arrival. The tolerance covers the
		// rounding between an authored second and the fixed-tick grid it is
		// measured against, so an occurrence scheduled exactly on a tick
		// boundary is never missed.
		if (elapsedSeconds + StationScheduleTolerance < firstOccurrenceSeconds) return 0;
		// A zero recurrence is a one-off: exactly one occurrence, ever.
		if (recurrenceSeconds <= 0.0) return 1;
		auto const steps = std::floor((elapsedSeconds + StationScheduleTolerance
			- firstOccurrenceSeconds) / recurrenceSeconds);
		if (!(steps > 0.0)) return 1;
		return static_cast<uint64_t>(steps) + 1;
	}
	// The installations registered with the shared availability contract
	// (#413): generators, coolant loops, chillers, heat engines and
	// Lighting fixtures (#928). They register their stable ids; an outage may
	// target only a registered identity, so the contract is what an
	// installation opts into rather than a special-cased outage lookup.
	// Later capacity-bearing installations join by adding their identity
	// here. Because the outage contract carries only an installationId and
	// no kind (#576), this namespace is required to be unambiguous: ids
	// must be unique across all registered kinds, which
	// validateStationSystemsSettings enforces, so every registered id
	// names exactly one machine.
	std::vector<StationInstallationIdentity> stationInstallationIdentities(
		StationSystemsSettings const& settings)
	{
		std::vector<StationInstallationIdentity> identities;
		for (auto const& generator : settings.generators)
			identities.push_back({generator.id, "generator"});
		for (auto const& loop : settings.coolantLoops)
			identities.push_back({loop.id, "coolant loop"});
		for (auto const& chiller : settings.chillers)
			identities.push_back({chiller.id, "chiller"});
		// The heat engine is a registered station installation from #807:
		// this loop is what lets an outage or maintenance job name an
		// individual engine, and what puts the engine's id into the one
		// installation namespace the ambiguity rules are judged over.
		for (auto const& engine : settings.heatEngines)
			identities.push_back({engine.id, "heat engine"});
		for (auto const& fixture : settings.lightingFixtures)
			identities.push_back({fixture.id, "lighting fixture"});
		return identities;
	}
	// #849: the fault modes one Installation can actually be injected with,
	// read straight from its own opt-ins. The editor offers injection from
	// this list rather than from a restated table, so a mode nobody opted in
	// is never even presented, and a mode the core would refuse cannot be
	// reached from the surface. Wear modes are deliberately absent: Wear is
	// accumulated by duty, never injected, and Repair never reverses it.
	std::vector<std::string> stationInjectableFaultModes(
		StationSystemsSettings const& settings, std::string_view installationId)
	{
		std::vector<std::string> modes;
		auto const named = [&](auto const& entries) {
			for (auto const& entry : entries) if (entry.id == installationId) return &entry;
			return static_cast<decltype(&entries.front())>(nullptr);
		};
		if (auto const* loop = named(settings.coolantLoops))
		{
			if (loop->coolantLeak) modes.push_back("coolant-leak");
			if (loop->condition) modes.push_back("pump-seizure");
		}
		if (auto const* chiller = named(settings.chillers))
		{
			if (chiller->compressorFailure) modes.push_back("compressor-failure");
			if (chiller->refrigerantLeak) modes.push_back("refrigerant-leak");
		}
		if (auto const* radiator = named(settings.radiators))
			if (radiator->panelPuncture) modes.push_back("panel-puncture");
		if (auto const* fixture = named(settings.lightingFixtures))
			if (fixture->fixtureFailure) modes.push_back("fixture-failure");
		if (auto const* engine = named(settings.heatEngines))
			if (engine->mechanicalSeizure) modes.push_back("mechanical-seizure");
		if (auto const* generator = named(settings.generators))
			if (generator->failedStart) modes.push_back("failed-start");
		if (auto const* array = named(settings.solarArrays))
		{
			if (array->stringLoss) modes.push_back("string-loss");
			if (array->trackingFailure) modes.push_back("tracking-failure");
		}
		if (auto const* battery = named(settings.batteries))
			if (battery->thermalRunaway) modes.push_back("thermal-runaway");
		if (auto const* air = named(settings.atmospheres))
			if (air->fire) modes.push_back("fire");
		if (auto const* cycle = named(settings.airlockCycles))
		{
			if (cycle->interruptedCycle) modes.push_back("interrupted-cycle");
			if (cycle->interlockFailure) modes.push_back("interlock-failure");
		}
		if (auto const* treatment = named(settings.waterTreatments))
			if (treatment->breakthrough) modes.push_back("breakthrough");
		if (auto const* reactor = named(settings.sabatierReactors))
			if (reactor->overtempRunaway) modes.push_back("overtemp-runaway");
		if (auto const* processor = named(settings.methaneProcessors))
			if (processor->overtempRunaway) modes.push_back("overtemp-runaway");
		std::sort(modes.begin(), modes.end());
		return modes;
	}
	// #849: the repair plans authored for one Installation (#842). Which
	// modes are repairable, how long each takes and what it costs is the
	// machine's own wiring; empty means there is none, which is exactly why
	// repairFault would refuse that machine. The editor reads repairability
	// from here rather than guessing, so a button never offers work the core
	// would turn down.
	std::vector<FaultRepairSettings> stationRepairPlans(
		StationSystemsSettings const& settings, std::string_view installationId)
	{
		auto const plansOf = [&](auto const& entries) {
			for (auto const& entry : entries)
				if (entry.id == installationId)
					return entry.repairParts
						? entry.repairParts->repairs
						: std::vector<FaultRepairSettings>{};
			return std::vector<FaultRepairSettings>{};
		};
		auto plans = plansOf(settings.coolantLoops);
		if (plans.empty()) plans = plansOf(settings.chillers);
		if (plans.empty()) plans = plansOf(settings.radiators);
		if (plans.empty()) plans = plansOf(settings.heatEngines);
		if (plans.empty()) plans = plansOf(settings.lightingFixtures);
		return plans;
	}
	// The endpoint half of the transfer entry check (#640): exactly the rules
	// a link between a store connector and a transfer connector can violate.
	// The whole-configuration validator below runs it for every transfer, and
	// the link-evaluation seam runs it for the single entry a candidate link
	// writes, so the rule lives in exactly one place and a refusal always
	// carries this rule's own wording. Identity and rate rules stay with the
	// whole-entry check because no connector can violate them.
	//
	// ignoreUnconnected is set only by the link seam: the endpoint the link
	// did not write is the user's next action, not this link's defect, so an
	// open endpoint is tolerated and the pair-level rules — same store on
	// both ends, unit agreement across the pair, composition and hazard
	// boundaries — are judged once both ends are connected. The whole-
	// configuration validator never sets it: an incomplete transfer is
	// invalid as a configuration.
	bool stationRequiresExtendedWearSchema(StationSystemsSettings const& settings)
	{
		auto extended = [](auto const& model) {
			return model && (model->fadeHalfLifeHours != 0.0 || model->initialAgeHours != 0.0);
		};
		for (auto const& machine : settings.batteries)
			if (extended(machine.capacityFade)) return true;
		for (auto const& machine : settings.coolantLoops)
			if (extended(machine.wear)) return true;
		for (auto const& machine : settings.heatSources)
			if (machine.wear) return true;
		for (auto const& machine : settings.solarArrays)
			if (extended(machine.cellDegradation)) return true;
		for (auto const& machine : settings.generators)
			if (extended(machine.fouledInjector)) return true;
		for (auto const& machine : settings.heatEngines)
			if (extended(machine.efficiencyDrift)) return true;
		for (auto const& machine : settings.electrolyzers)
			if (extended(machine.membraneDegradation)) return true;
		for (auto const& machine : settings.waterTreatments)
			if (extended(machine.membraneFouling)) return true;
		for (auto const& machine : settings.co2Captures)
			if (extended(machine.absorbentDegradation)) return true;
		for (auto const& machine : settings.fuelCells)
			if (extended(machine.stackDegradation)) return true;
		for (auto const& machine : settings.sabatierReactors)
			if (extended(machine.catalystDeactivation)) return true;
		for (auto const& machine : settings.methaneProcessors)
			if (extended(machine.catalystDeactivation)) return true;
		return false;
	}

}
