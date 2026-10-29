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
    Installation source(1, 1);
    source.setHullType(std::nullopt);
    auto yaml = source.serialize();
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
    auto thermal = nsi::systems::expandThermal(installation);
    settings.hull = thermal.hull;
    settings.thermalLinks = thermal.links;
    settings.solarCycle = thermal.solarCycle;
    require(settings.atmospherePathways.empty(), "bridge invented gas pathways");
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
void noHullIsolation() {
    Installation installation(2, 1);
    installation.setHullType(std::nullopt);
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
void hullCooling() {
    auto cooling=[](Installation installation) {
        auto settings=scene(installation);
        station::StationSystemsRuntime runtime(settings,0.25f);
        advance(runtime,60);
        const auto snapshot=runtime.snapshot();
        require(snapshot.thermal.hull.has_value(),"Hull missing from runtime");
        require(snapshot.thermal.hull->absorbedKW==0.0
            && snapshot.thermal.hull->absorbedEnergyKJ==0.0,"Dark sky must absorb exactly zero");
        require(snapshot.thermal.hull->emittedEnergyKJ>0.0,"Hull must radiate to space");
        return snapshot.atmospheres[0].temperatureC;
    };
    Installation single(1,1);
    single.setHullType(Installation::HullType::BareSkin);
    const double bare=cooling(single);
    single.setHullType(Installation::HullType::MultiLayerBlanket);
    const double blanket=cooling(single);
    require(bare < blanket-1.0 && blanket < 20.0,"Bare skin must cool measurably faster");
    // Same air volume; removing two side panels reduces the total exterior area.
    Installation half=single;
    half.toggleWall(0,half.level(0).horizontalWall(0,0));
    half.toggleWall(0,half.level(0).horizontalWall(0,1));
    require(cooling(half)>blanket,"More skin must cool faster at equal air capacity");
    Installation roofOnly=single;
    for (std::size_t w=0;w<roofOnly.level(0).walls().size();++w) roofOnly.toggleWall(0,w);
    const double roofTemperature=cooling(roofOnly);
    require(roofTemperature<19.999,"Roof and underside alone must cool the compartment");

    // Compare the complete default skin against the previous side-only tracer
    // through the real runtime, keeping the same air and hull assembly.
    Installation standard;
    auto complete=scene(standard);
    auto sideOnly=complete;
    const double fraction=standard.sideExteriorAreaM2(0,0)
        / standard.exteriorSkin(0,0).totalM2();
    sideOnly.hull->emittingAreaM2*=fraction;
    sideOnly.hull->projectedAreaM2*=fraction;
    sideOnly.hull->heatCapacityKJPerK*=fraction;
    sideOnly.thermalLinks[0].conductanceKWPerK*=fraction;
    station::StationSystemsRuntime fullRuntime(complete,0.25f), sideRuntime(sideOnly,0.25f);
    advance(fullRuntime,60); advance(sideRuntime,60);
    const double fullLoss=20-fullRuntime.snapshot().atmospheres[0].temperatureC;
    const double sideLoss=20-sideRuntime.snapshot().atmospheres[0].temperatureC;
    require(fullLoss>5*sideLoss && fullLoss<7*sideLoss,
        "Roof and underside must increase default cooling roughly sixfold");
    // A genuine v3 load retains the exact previous no-link behaviour.
    auto yaml=single.serialize();
    yaml.replace(yaml.find("version: 4"),10,"version: 3");
    const auto start=yaml.find("hull:"); yaml.erase(start,yaml.find('\n',start)-start+1);
    Installation legacy; legacy.deserialize(yaml);
    auto settings=scene(legacy);
    require(!settings.hull && settings.thermalLinks.empty(),"Legacy scene gained coupling");
    station::StationSystemsRuntime runtime(settings,0.25f);
    advance(runtime,60); near(runtime.snapshot().atmospheres[0].temperatureC,20.0);
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
    try { mapping(); noHullIsolation(); occupantsAndLimits(); hullCooling(); }
    catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
