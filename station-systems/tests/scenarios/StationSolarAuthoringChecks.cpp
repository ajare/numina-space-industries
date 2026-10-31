#include "Smoke.h"
#include "station/StationSystemsRuntime.h"
#include "station/StationConnections.h"

#include <cmath>
#include <limits>
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
            auto const snapshot = runtime.snapshot();
            close(snapshot.power.sources[0].availableKW, enabled ? area * packing * efficiency * irradiance / 1000.0 : 0.0);
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
        close(runtime.snapshot().power.sources[0].availableKW, 22.21152);
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
    close(measure(false), 22.21152);
    close(measure(true), 22.21152 * 0.9);
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
}
