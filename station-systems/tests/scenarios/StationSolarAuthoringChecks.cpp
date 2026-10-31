#include "Smoke.h"
#include "station/StationSystemsRuntime.h"
#include "station/StationConnections.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

namespace
{
void solarArrayDerivedGeneration(smoke::Context const&)
{
    auto close = [](double actual, double expected) {
        smoke::require(std::abs(actual - expected) < 1e-9, "solar availability differs from expected generation");
    };
    for (auto const technology : {station::SolarCellTechnology::TripleJunctionGaAs,
            station::SolarCellTechnology::SingleJunctionGaAs, station::SolarCellTechnology::SpaceSilicon})
    for (double area : {0.0, 60.0, 123.0})
    for (bool enabled : {false, true})
    for (double sunlight : {0.0, 0.25, 1.0})
    {
        station::StationSystemsSettings settings;
        settings.enabled = true;
        station::SolarArraySettings array;
        array.id = "derived";
        array.capacityKW = 999.0; // Must never leak into derived generation.
        array.areaM2 = area;
        array.cellTechnology = technology;
        array.enabled = enabled;
        auto legacy = array;
        legacy.id = "legacy";
        legacy.areaM2.reset();
        legacy.capacityKW = 20.0;
        settings.solarArrays = {array, legacy};
        settings.solarCycle.emplace().periodSeconds = 10;
        settings.solarCycle->segments = {{0, sunlight}};
        station::StationSystemsRuntime runtime(settings, station::StationSystemsUpdateMode::Variable);
        // No star input defaults to zero, irrespective of the authored cycle.
        smoke::require(runtime.update(1.0) && runtime.snapshot().healthy, "default irradiance step failed");
        close(runtime.snapshot().power.sources[0].availableKW, 0.0);
        for (double irradiance : {1361.0, 680.5, 0.0, 9086.0})
        {
            smoke::require(runtime.setDirectSolarIrradianceWPerM2(irradiance), "valid irradiance refused");
            for (double invalid : {-1.0, std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::quiet_NaN()})
                smoke::require(!runtime.setDirectSolarIrradianceWPerM2(invalid), "invalid irradiance accepted");
            smoke::require(runtime.update(1.0) && runtime.snapshot().healthy, "mixed solar step failed");
            double efficiency = technology == station::SolarCellTechnology::TripleJunctionGaAs ? 0.32
                : technology == station::SolarCellTechnology::SingleJunctionGaAs ? 0.19 : 0.14;
            double packing = technology == station::SolarCellTechnology::SpaceSilicon ? 0.80 : 0.85;
            // Blanket-level derate between bare-cell datasheet efficiency and built-array
            // output; silicon-era assembly is the lossier of the two.
            double derate = technology == station::SolarCellTechnology::SpaceSilicon ? 0.75 : 0.85;
            auto const snapshot = runtime.snapshot();
            close(snapshot.power.sources[0].availableKW,
                enabled ? area * packing * efficiency * derate * irradiance / 1000.0 : 0.0);
            close(snapshot.power.sources[1].availableKW, enabled ? 20.0 * sunlight : 0.0);
            smoke::require(runtime.effectiveSettings().solarArrays[0] == array, "generation changed authored array");
        }
    }
    // A throttled, lossy connection must not clip the array's own readout.
    {
        station::StationSystemsSettings settings;
        settings.enabled = true;
        station::SolarArraySettings array;
        array.id = "sun";
        array.areaM2 = 60.0;
        settings.solarArrays = {array};
        size_t constrained = 0;
        for (auto const& link : station::station_connections::build(settings).links)
            if (link.designKey.ownerType == "Solar") {
                auto& connection = settings.connections[link.designKey];
                connection.maximumThroughput = 1.0;
                connection.efficiency = 0.5;
                ++constrained;
            }
        smoke::require(constrained > 0, "solar connection fixture did not constrain a link");
        station::StationSystemsRuntime runtime(settings, station::StationSystemsUpdateMode::Variable);
        smoke::require(runtime.setDirectSolarIrradianceWPerM2(1361.0)
            && runtime.update(1.0) && runtime.snapshot().healthy, "clipped solar step failed");
        close(runtime.snapshot().power.sources[0].availableKW, 18.879792);
        smoke::require(runtime.snapshot().power.poolGenerationKW < 1.0,
            "solar connection did not clip bus-side availability");
    }
    // Calendar degradation must scale derived capacity, even with zero declared rating.
    auto measure = [](bool wear) {
        station::StationSystemsSettings settings;
        settings.enabled = true;
        station::SolarArraySettings array;
        array.id = "sun";
        array.areaM2 = 60.0;
        array.capacityKW = 0.0;
        if (wear) {
            array.cellDegradation.emplace().fadePerCalendarHour = 0.1;
        }
        settings.solarArrays = {array};
        station::StationSystemsRuntime runtime(settings, station::StationSystemsUpdateMode::Variable);
        smoke::require(runtime.setDirectSolarIrradianceWPerM2(1361.0), "irradiance refused");
        smoke::require(runtime.update(3600.0) && runtime.update(1.0) && runtime.snapshot().healthy,
            "wear solar step failed");
        return runtime.snapshot().power.sources[0].availableKW;
    };
    close(measure(false), 18.879792);
    close(measure(true), 18.879792 * 0.9);
}

// #68: a fast-saturating wear fixture. A fade of 1.0 per calendar hour
// drives an array to its maxWearFraction ceiling inside one simulated
// hour, so a fully worn array is reached without a long wall-clock run.
// The pair - one derived array, one legacy array - runs together so the
// two modes are read under identical wear. Each mode reads its own light:
// the derived array takes direct irradiance, the legacy array the authored
// solar cycle, so a dark run has to put out both.
struct WornPairRun
{
    station::StationSystemsSnapshot snapshot;
    // Events announced after the ledger saturated, i.e. everything the
    // worn arrays had to say once they were dead.
    std::vector<station::StationSystemsEvent> lateEvents;
};

WornPairRun runWornPair(double irradiance, double cycleLevel, std::optional<double> wearCeiling)
{
    station::StationSystemsSettings settings;
    settings.enabled = true;
    station::SolarArraySettings derived;
    derived.id = "derived";
    derived.areaM2 = 60.0;
    derived.cellTechnology = station::SolarCellTechnology::TripleJunctionGaAs;
    derived.capacityKW = 999.0; // Must never reach the derived nameplate.
    auto legacy = derived;
    legacy.id = "legacy";
    legacy.areaM2.reset();
    legacy.capacityKW = 20.0;
    if (wearCeiling)
        for (auto* array : {&derived, &legacy})
        {
            array->cellDegradation.emplace().fadePerCalendarHour = 1.0;
            array->cellDegradation->maxWearFraction = *wearCeiling;
        }
    settings.solarArrays = {derived, legacy};
    settings.solarCycle.emplace().constantLevel = cycleLevel;
    station::StationSystemsRuntime runtime(settings, station::StationSystemsUpdateMode::Variable);
    smoke::require(runtime.setDirectSolarIrradianceWPerM2(irradiance), "irradiance refused");
    // The hour saturates the ledger while the array is still lit, so the
    // death is caused by wear and never by the light going out.
    smoke::require(runtime.update(3600.0) && runtime.snapshot().healthy,
        "wear saturation step failed");
    auto const mark = runtime.events().size();
    for (int step = 0; step < 4; ++step)
        smoke::require(runtime.update(1.0) && runtime.snapshot().healthy,
            "post-saturation step failed");
    auto const& events = runtime.events();
    return {runtime.snapshot(),
        std::vector<station::StationSystemsEvent>(events.begin() + static_cast<std::ptrdiff_t>(mark),
            events.end())};
}

bool announcedSolarTransition(std::vector<station::StationSystemsEvent> const& events)
{
    for (auto const& event : events)
        if (event.category == "solar") return true;
    return false;
}

void solarArrayWearDisablesDerivedArray(smoke::Context const&)
{
    auto const fullSunNameplateKW = 60 * 0.85 * 0.32 * 0.85 * 1361.0 / 1000.0;
    // Both modes at the same 100% wear ceiling reach the same verdict.
    auto const worn = runWornPair(1361.0, 1.0, 1.0);
    for (auto const& source : worn.snapshot.power.sources)
    {
        smoke::require(source.wear.wearFraction == 1.0, "wear fixture did not reach its ceiling");
        smoke::require(source.disabled,
            "a fully worn array reported itself as not disabled (derived and legacy alike)");
        smoke::require(source.availableKW == 0.0, "a fully worn array still delivered power");
    }
    // A dead array cannot transition, so it must not keep the event log
    // describing a day/night cycle it no longer has.
    smoke::require(!announcedSolarTransition(worn.lateEvents),
        "a fully worn array announced an eclipse transition");
    // Half-worn is diminished, not dead: the array still delivers, and
    // the disabled test must not fire early.
    auto const halfWorn = runWornPair(1361.0, 1.0, 0.5);
    smoke::require(!halfWorn.snapshot.power.sources[0].disabled
        && !halfWorn.snapshot.power.sources[1].disabled,
        "a half-worn array reported itself as disabled");
    smoke::require(std::abs(halfWorn.snapshot.power.sources[0].availableKW
        - fullSunNameplateKW * 0.5) < 1e-9, "half-worn derived array did not deliver half its nameplate");
    smoke::require(halfWorn.snapshot.power.sources[1].availableKW > 0.0,
        "half-worn legacy array stopped delivering");
    // Unworn in full sun is neither disabled nor dark.
    auto const healthy = runWornPair(1361.0, 1.0, std::nullopt);
    for (auto const& source : healthy.snapshot.power.sources)
        smoke::require(!source.disabled, "an unworn lit array reported itself as disabled");
    smoke::require(std::abs(healthy.snapshot.power.sources[0].availableKW - fullSunNameplateKW) < 1e-9,
        "unworn derived array did not deliver its full nameplate");
    // Zero illumination alone never marks an array disabled: an unlit
    // array is in eclipse, not switched off.
    auto const dark = runWornPair(0.0, 0.0, std::nullopt);
    for (auto const& source : dark.snapshot.power.sources)
    {
        smoke::require(source.availableKW == 0.0, "a dark array delivered power");
        smoke::require(!source.disabled, "zero illumination was reported as disabled");
    }
    // A derived blanket with no area at all is still zero installed
    // capacity, and stays disabled with the light on.
    {
        station::StationSystemsSettings settings;
        settings.enabled = true;
        station::SolarArraySettings array;
        array.id = "no-blanket";
        array.areaM2 = 0.0;
        settings.solarArrays = {array};
        station::StationSystemsRuntime runtime(settings, station::StationSystemsUpdateMode::Variable);
        smoke::require(runtime.setDirectSolarIrradianceWPerM2(1361.0) && runtime.update(1.0)
            && runtime.snapshot().healthy, "zero-area solar step failed");
        smoke::require(runtime.snapshot().power.sources[0].disabled,
            "a zero-area derived array stopped reporting disabled");
    }
}

// #69: the published eclipse state is the sky's verdict, never a restatement
// of zero output. Each run pairs a derived blanket (which reads the pushed
// direct irradiance) with a legacy rating (which reads the authored solar
// cycle), so "the light went out" has to mean the same thing for both light
// sources, and every other cause of a zero has to keep its own name.
station::StationSystemsSnapshot runEclipsePair(double irradiance, double cycleLevel,
    bool withTrackingFailure)
{
    station::StationSystemsSettings settings;
    settings.enabled = true;
    station::SolarArraySettings derived;
    derived.id = "derived";
    derived.areaM2 = 60.0;
    derived.cellTechnology = station::SolarCellTechnology::TripleJunctionGaAs;
    derived.capacityKW = 999.0; // Must never reach the derived nameplate.
    auto legacy = derived;
    legacy.id = "legacy";
    legacy.areaM2.reset();
    legacy.capacityKW = 20.0;
    if (withTrackingFailure)
        for (auto* array : {&derived, &legacy})
            array->trackingFailure.emplace().availability = 0.0;
    settings.solarArrays = {derived, legacy};
    settings.solarCycle.emplace().constantLevel = cycleLevel;
    station::StationSystemsRuntime runtime(settings, station::StationSystemsUpdateMode::Variable);
    smoke::require(runtime.setDirectSolarIrradianceWPerM2(irradiance), "irradiance refused");
    smoke::require(runtime.update(1.0) && runtime.snapshot().healthy, "eclipse fixture step failed");
    if (withTrackingFailure)
    {
        smoke::require(runtime.injectFault("derived", "tracking-failure", {})
            && runtime.injectFault("legacy", "tracking-failure", {}), "tracking-failure refused");
        smoke::require(runtime.update(1.0) && runtime.snapshot().healthy, "faulted eclipse step failed");
    }
    return runtime.snapshot();
}

void solarEclipseStateIsIllumination(smoke::Context const&)
{
    // Full sun in both modes: generating, and nothing claims a shadow.
    auto const lit = runEclipsePair(1361.0, 1.0, false);
    for (auto const& source : lit.power.sources)
    {
        smoke::require(!source.eclipse, "a lit array published an eclipse");
        smoke::require(source.availableKW > 0.0, "a lit array delivered nothing");
    }
    // The shadow: the planet blocks the star, so both lights go out at once.
    auto const dark = runEclipsePair(0.0, 0.0, false);
    for (auto const& source : dark.power.sources)
    {
        smoke::require(source.eclipse, "a blocked star was not published as an eclipse");
        smoke::require(!source.disabled, "an eclipse was reported as disabled");
        smoke::require(source.availableKW == 0.0, "an eclipsed array delivered power");
    }
    // A tracking failure pinned at zero availability stops the panel, not
    // the sun. The array still stands in daylight and has to say so.
    auto const stuck = runEclipsePair(1361.0, 1.0, true);
    for (auto const& source : stuck.power.sources)
    {
        smoke::require(source.availableKW == 0.0,
            "tracking-failure at availability 0 still delivered power");
        smoke::require(!source.disabled, "a tracking failure switched the array off");
        smoke::require(!source.eclipse, "a tracking failure published an eclipse");
        smoke::require(source.activeFaults.size() == 1,
            "the fault ledger did not reach the snapshot with the zero");
    }
    // Penumbra: a sliver of light is not a blocked star, however small the
    // reading it produces.
    auto const penumbra = runEclipsePair(0.0368, 1.0, false);
    smoke::require(!penumbra.power.sources[0].eclipse, "penumbra published an eclipse");
    smoke::require(penumbra.power.sources[0].availableKW > 0.0
        && penumbra.power.sources[0].availableKW < 0.05,
        "penumbra fixture did not land under the readable floor");
    // Wear is the machine spending itself, in full sun: disabled, never dark.
    // A dead array cannot be shadowed either - the eclipse test is behind the
    // disabled one, so it never claims a night it cannot have.
    auto const worn = runWornPair(1361.0, 1.0, 1.0);
    for (auto const& source : worn.snapshot.power.sources)
    {
        smoke::require(source.disabled, "a fully worn array reported itself as not disabled");
        smoke::require(!source.eclipse, "a fully worn array published an eclipse");
    }
}

void solarArrayEnableDisable(smoke::Context const&)
{
    // Exercise both modes together through settings, stepping and snapshots only.
    station::StationSystemsSettings settings;
    settings.enabled = true;
    station::SolarArraySettings derived;
    derived.id = "derived";
    derived.areaM2 = 60.0;
    derived.cellTechnology = station::SolarCellTechnology::SpaceSilicon;
    derived.capacityKW = 999.0;
    auto legacy = derived;
    legacy.id = "legacy";
    legacy.areaM2.reset();
    legacy.capacityKW = 20.0;
    settings.solarArrays = {derived, legacy};
    settings.solarCycle.emplace().periodSeconds = 10;
    settings.solarCycle->segments = {{0, 0.5}};
    station::StationSystemsRuntime runtime(settings, station::StationSystemsUpdateMode::Variable);
    smoke::require(runtime.setDirectSolarIrradianceWPerM2(1361.0), "irradiance refused");
    auto step = [&]() {
        smoke::require(runtime.update(1.0) && runtime.snapshot().healthy, "enable/disable solar step failed");
        return runtime.snapshot().power;
    };
    auto const original = step();
    smoke::require(original.sources[0].availableKW > 0.0 && original.sources[1].availableKW > 0.0,
        "enabled arrays must generate");
    for (auto const index : {0, 1})
    {
        settings.solarArrays[index].enabled = false;
        smoke::require(runtime.updateSettings(settings), "solar disable refused");
        auto const disabled = step();
        smoke::require(disabled.sources[index].disabled && disabled.sources[index].availableKW == 0.0,
            "disabled array generated or failed to report disabled");
        smoke::require(disabled.sources[index].capacityKW == original.sources[index].capacityKW,
            "disabling changed capacity");
        smoke::require(disabled.sources[1 - index] == original.sources[1 - index],
            "disabling one array changed the other mode");
        smoke::require(runtime.effectiveSettings().solarArrays == settings.solarArrays,
            "disabling changed authored size, technology or rating");
        settings.solarArrays[index].enabled = true;
        smoke::require(runtime.updateSettings(settings), "solar re-enable refused");
        smoke::require(step().sources == original.sources, "re-enabling did not restore exact output");
    }
    // An enabled array without illumination is not switched off.
    settings.solarCycle->segments = {{0, 0.0}};
    station::StationSystemsRuntime dark(settings, station::StationSystemsUpdateMode::Variable);
    smoke::require(dark.setDirectSolarIrradianceWPerM2(0.0) && dark.update(1.0) && dark.snapshot().healthy,
        "zero illumination step failed");
    for (auto const& source : dark.snapshot().power.sources)
        smoke::require(!source.disabled && source.availableKW == 0.0,
            "zero illumination incorrectly reported as disabled");
}
}

void registerStationSolarAuthoringChecks(std::vector<smoke::Check>& checks)
{
    checks.push_back({"solarArrayDerivedGeneration", solarArrayDerivedGeneration});
    checks.push_back({"solarArrayEnableDisable", solarArrayEnableDisable});
    checks.push_back({"solarArrayWearDisablesDerivedArray", solarArrayWearDisablesDerivedArray});
    checks.push_back({"solarEclipseStateIsIllumination", solarEclipseStateIsIllumination});
}
