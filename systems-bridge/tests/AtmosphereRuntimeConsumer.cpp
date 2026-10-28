#include <nsi/systems/Atmosphere.h>
#include "StationScenarioRig.h"
#include <iostream>

namespace {
using Installation = nsi::core::Installation;
using smoke::station::require;
void near(double actual, double expected) {
    require(std::isfinite(actual) && std::abs(actual - expected)
        <= 1e-10 * std::max(1.0, std::abs(expected)), "numeric mismatch");
}
// Author through the public document boundary, not mutable geometry internals.
Installation assigned(std::string const& loads) {
    auto yaml = Installation(1, 1).serialize();
    const auto key = yaml.find("atmosphere: Earth-like");
    require(key != std::string::npos, "missing preset");
    const auto line = yaml.rfind('\n', key) + 1;
    const auto end = yaml.find('\n', key);
    yaml.insert(end, "\n" + std::string(key - line, ' ') + "atmosphereLoads: {" + loads + "}");
    Installation result;
    result.deserialize(yaml);
    return result;
}
station::StationSystemsSettings scene(Installation const& installation) {
    station::StationSystemsSettings settings;
    settings.enabled = true;
    settings.systemsStepWorldTicks = 1;
    settings.observationIntervalSeconds = 1;
    settings.atmospheres = nsi::systems::expandInstallation(installation);
    require(settings.thermalLinks.empty() && settings.atmospherePathways.empty(),
        "bridge invented connections");
    return settings;
}
void advance(station::StationSystemsRuntime& runtime, double seconds) {
    const bool ok = runtime.advanceTo(seconds);
    const auto diagnostics = runtime.diagnostics();
    require(ok, diagnostics.empty() ? "scenario failed" : diagnostics.front().message.c_str());
    require(runtime.snapshot().healthy, "unhealthy scenario");
}
void mapping() {
    auto installation = assigned("occupants: 3, oxygenUsePerOccupantKgPerS: 0.001, "
        "co2ProductionPerOccupantKgPerS: 0.002, sensibleHeatPerOccupantKW: 0.03, "
        "moistureAdditionPerOccupantKgPerS: 0.000004, leakRateKgPerS: 0.005, "
        "reservoirId: bus, exchangerConductanceKWPerK: 0.06, "
        "fire: {oxygenKgPerS: 0.007, carbonMonoxideKgPerS: 0.0008, "
        "carbonDioxideKgPerS: 0.009, heatKW: 0.1, oxygenThresholdKg: 0.11}");
    const auto& source = installation.level(0).sectors()[0].atmosphereAssignment;
    const auto a = *nsi::systems::expandSector(installation, 0, 0);
    require(a.occupants == source.occupants, "occupancy dropped");
    require(a.oxygenUsePerOccupantKgPerS == source.oxygenUsePerOccupantKgPerS
        && a.co2ProductionPerOccupantKgPerS == source.co2ProductionPerOccupantKgPerS
        && a.sensibleHeatPerOccupantKW == source.sensibleHeatPerOccupantKW
        && a.moistureAdditionPerOccupantKgPerS == source.moistureAdditionPerOccupantKgPerS
        && a.leakRateKgPerS == source.leakRateKgPerS
        && a.reservoirId == source.reservoirId
        && a.exchangerConductanceKWPerK == source.exchangerConductanceKWPerK,
        "assignment changed");
    require(a.fire.has_value(), "fire dropped");
    const auto& f = *source.fire;
    require(*a.fire == station::AtmosphereFireSettings{f.oxygenKgPerS,
        f.carbonMonoxideKgPerS, f.carbonDioxideKgPerS, f.heatKW, f.oxygenThresholdKg},
        "fire changed");
    auto settings = scene(installation);
    station::ThermalReservoirSettings reservoir;
    reservoir.id = "bus"; reservoir.thermalClass = "low";
    reservoir.heatCapacityKJPerK = 100; reservoir.startingTemperatureC = 0;
    reservoir.minTemperatureC = -100; reservoir.maxTemperatureC = 100;
    settings.thermalReservoirs = {reservoir};
    station::StationSystemsRuntime runtime(settings, 0.25f);
    // Fire settings enable this injectable fault; configuration alone does not ignite it.
    require(runtime.injectFault(a.id, "fire", {}), "configured fire could not ignite");
    advance(runtime, 1);
    const auto state = runtime.snapshot().atmospheres[0];
    require(state.oxygenKg < a.startingOxygenKg && state.carbonDioxideKg > a.startingCarbonDioxideKg,
        "loads did not reach simulation");
    require(state.leakedKg > 0 && state.fireOxygenConsumedKg > 0
        && state.carbonMonoxideKg > 0 && state.fireHeatKJ > 0, "leak/fire inactive");
    require(state.thermallyCoupled && state.lastExchangeIntoCompartmentKW < 0,
        "reservoir did not exchange heat");
}
void isolation() {
    Installation installation(2, 1);
    installation.toggleWall(0, installation.level(0).verticalWall(1, 0));
    auto settings = scene(installation);
    // Deliberately impose a gradient: any implicit shared-wall heat path is observable.
    settings.atmospheres[1].startingTemperatureC = 30;
    station::StationSystemsRuntime runtime(settings, 0.25f);
    for (double time : {0.25, 1.0, 10.0, 60.0}) {
        advance(runtime, time);
        const auto state = runtime.snapshot();
        for (std::size_t i = 0; i < 2; ++i) {
            near(state.atmospheres[i].temperatureC, settings.atmospheres[i].startingTemperatureC);
            require(!state.atmospheres[i].thermallyCoupled, "inert Sector coupled");
        }
    }
    const auto a = *nsi::systems::expandSector(installation, 0, 0);
    require(!a.fire && a.reservoirId.empty() && a.exchangerConductanceKWPerK == 0,
        "inert defaults changed");
}
void occupantsAndLimits() {
    const auto installation = assigned("occupants: 2, oxygenUsePerOccupantKgPerS: 0.05, "
        "co2ProductionPerOccupantKgPerS: 0.05");
    auto settings = scene(installation);
    const auto a = settings.atmospheres[0];
    const auto* spec = nsi::systems::specFor(Installation::AtmospherePreset::EarthLike);
    require(a.minPressureKPa == spec->minPressureKPa && a.maxPressureKPa == spec->maxPressureKPa
        && a.minOxygenPartialPressureKPa == spec->minOxygenPartialPressureKPa
        && a.maxCarbonDioxidePartialPressureKPa == spec->maxCarbonDioxidePartialPressureKPa,
        "limits changed");
    station::StationSystemsRuntime runtime(settings, 0.25f);
    advance(runtime, 0.25);
    auto state = runtime.snapshot().atmospheres[0];
    near(state.oxygenKg, a.startingOxygenKg - 0.025);
    near(state.carbonDioxideKg, a.startingCarbonDioxideKg + 0.025);
    require(!state.lowOxygen && !state.highCarbonDioxide, "premature limit crossing");
    advance(runtime, 12);
    state = runtime.snapshot().atmospheres[0];
    require(state.lowOxygen && state.oxygenPartialPressureKPa < a.minOxygenPartialPressureKPa,
        "oxygen limit clamped or unreported");
    require(state.highCarbonDioxide && state.carbonDioxidePartialPressureKPa > a.maxCarbonDioxidePartialPressureKPa,
        "CO2 limit clamped or unreported");

    auto leaking = scene(assigned("leakRateKgPerS: 0.2"));
    station::StationSystemsRuntime leak(leaking, 0.25f);
    advance(leak, 12);
    const auto low = leak.snapshot().atmospheres[0];
    require(low.belowPressureLimit && low.pressureKPa < a.minPressureKPa,
        "low pressure clamped or unreported");
    auto heating = scene(assigned("occupants: 1, sensibleHeatPerOccupantKW: 1"));
    station::StationSystemsRuntime heat(heating, 0.25f);
    advance(heat, 300);
    const auto high = heat.snapshot().atmospheres[0];
    require(high.abovePressureLimit && high.pressureKPa > a.maxPressureKPa,
        "high pressure clamped or unreported");
}
}
int main() {
    try { mapping(); isolation(); occupantsAndLimits(); }
    catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
