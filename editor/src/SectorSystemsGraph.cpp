#include "SectorSystemsGraph.h"

#include <nsi/systems/Atmosphere.h>
#include <station/StationConnections.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace station::editor {
namespace {
std::string number(double value, const char* unit = "") {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (std::isfinite(value)) out << std::setprecision(7) << value;
    else out << "--";
    if (*unit) out << ' ' << unit;
    return out.str();
}
void atmosphereFields(SectorSystemNode& node, const station::AtmosphereSettings& a) {
    auto field = [&](const char* label, double value, const char* unit) {
        node.configuration.emplace_back(label, number(value, unit));
    };
    node.configuration.emplace_back("Preset", "Earth-like");
    field("Volume", a.volumeM3, "m3");
    field("Starting temperature", a.startingTemperatureC, "C");
    field("Starting oxygen", a.startingOxygenKg, "kg");
    field("Starting nitrogen", a.startingNitrogenKg, "kg");
    field("Starting carbon dioxide", a.startingCarbonDioxideKg, "kg");
    field("Starting water vapour", a.startingWaterVapourKg, "kg");
    field("Thermal capacity (cv)", a.thermalCapacityKJPerK, "kJ/K");
    field("Advected specific heat (cp)", a.advectedSpecificHeatKJPerKgK, "kJ/(kg K)");
    field("Minimum pressure", a.minPressureKPa, "kPa");
    field("Maximum pressure", a.maxPressureKPa, "kPa");
    field("Minimum oxygen partial pressure", a.minOxygenPartialPressureKPa, "kPa");
    field("Maximum CO2 partial pressure", a.maxCarbonDioxidePartialPressureKPa, "kPa");
    field("Occupants", a.occupants, "");
    field("Oxygen use per occupant", a.oxygenUsePerOccupantKgPerS, "kg/s");
    field("CO2 production per occupant", a.co2ProductionPerOccupantKgPerS, "kg/s");
    field("Sensible heat per occupant", a.sensibleHeatPerOccupantKW, "kW");
    field("Moisture per occupant", a.moistureAdditionPerOccupantKgPerS, "kg/s");
    field("Leak rate", a.leakRateKgPerS, "kg/s");
    node.configuration.emplace_back("Reservoir", a.reservoirId.empty() ? "None (adiabatic)" : a.reservoirId);
    field("Exchanger conductance", a.exchangerConductanceKWPerK, "kW/K");
    node.configuration.emplace_back("Fire fault opt-in", a.fire ? "Enabled (not ignited at startup)" : "Disabled");
    if (a.fire) {
        field("Fire oxygen consumption", a.fire->oxygenKgPerS, "kg/s");
        field("Fire carbon monoxide", a.fire->carbonMonoxideKgPerS, "kg/s");
        field("Fire carbon dioxide", a.fire->carbonDioxideKgPerS, "kg/s");
        field("Fire heat", a.fire->heatKW, "kW");
        field("Fire oxygen threshold", a.fire->oxygenThresholdKg, "kg");
    }
}

void atmosphereSimulation(SectorSystemNode& node, const station::StationSystemsSnapshot* snapshot) {
    node.simulationStatus = "Unavailable: no matching simulation preview.";
    if (!snapshot) return;
    if (!snapshot->enabled) {
        node.simulationStatus = "Unavailable: simulation is disabled.";
        return;
    }
    const auto it = std::find_if(snapshot->atmospheres.begin(), snapshot->atmospheres.end(),
        [&](const auto& atmosphere) { return atmosphere.id == node.id; });
    if (it == snapshot->atmospheres.end()) {
        node.simulationStatus = "Unavailable: compartment is absent from the simulation.";
        return;
    }
    const auto& a = *it;
    node.simulationStatus = "As of " + number(snapshot->elapsedSeconds, "s") +
        (snapshot->healthy ? "" : " (last committed state; simulation failed)");
    auto field = [&](const char* label, double value, const char* unit) {
        node.simulation.emplace_back(label, number(value, unit));
    };
    field("Temperature", a.temperatureC, "C");
    field("Pressure", a.pressureKPa, "kPa");
    field("Relative humidity", a.relativeHumidity * 100, "%");
    auto gas = [&](const char* label, double kg, double partialPressure) {
        const auto fraction = std::isfinite(a.pressureKPa) && a.pressureKPa > 0
            ? number(100 * partialPressure / a.pressureKPa, "%") : "-- %";
        node.simulation.emplace_back(label, number(kg, "kg") + " | " + fraction +
            " | " + number(partialPressure, "kPa"));
    };
    // Ideal-gas partial/total pressure is the wet mole/volume fraction, not
    // mass fraction. CO is retained hazard inventory, outside these four gases.
    node.simulation.emplace_back("Gas composition", "mass | wet mole fraction | partial pressure");
    gas("Oxygen", a.oxygenKg, a.oxygenPartialPressureKPa);
    gas("Nitrogen", a.nitrogenKg, a.nitrogenPartialPressureKPa);
    gas("Carbon dioxide", a.carbonDioxideKg, a.carbonDioxidePartialPressureKPa);
    gas("Water vapour", a.waterVapourKg, a.waterVapourPartialPressureKPa);
    field("Carbon monoxide", a.carbonMonoxideKg, "kg");
    field("Thermal energy", a.currentEnergyKJ, "kJ");
    field("Condensed water (total)", a.condensedKg, "kg");
    field("Leaked gas (total)", a.leakedKg, "kg");
    field("Occupant heat", a.lastOccupantHeatKW, "kW");
    field("Reservoir exchange (+ into air)", a.lastExchangeIntoCompartmentKW, "kW");
    std::string conditions;
    auto condition = [&](bool active, const char* text) {
        if (!active) return;
        if (!conditions.empty()) conditions += ", ";
        conditions += text;
    };
    condition(a.belowPressureLimit, "Low pressure");
    condition(a.abovePressureLimit, "High pressure");
    condition(a.lowOxygen, "Low oxygen");
    condition(a.highCarbonDioxide, "High CO2");
    condition(a.condensing, "Condensing");
    node.simulation.emplace_back("Conditions", conditions.empty() ? "Within configured limits" : conditions);
}
}

SectorSystemsGraph buildSectorSystemsGraph(const nsi::core::Installation& installation,
                                          std::size_t level, std::size_t sector,
                                          const station::StationSystemsSnapshot* snapshot) {
    SectorSystemsGraph result;
    if (level >= installation.levelCount() || sector >= installation.level(level).sectors().size()) {
        result.message = "No Sector selected.";
        return result;
    }
    const auto& source = installation.level(level).sectors()[sector];
    result.identity = std::to_string(level) + ":" + std::to_string(source.uid);
    result.title = "Level " + std::to_string(level) + " / " +
        (source.name.empty() ? "Sector " + std::to_string(source.uid) : source.name);
    const auto atmosphere = nsi::systems::expandSector(installation, level, sector);
    if (!atmosphere) {
        result.message = source.type == nsi::core::Installation::SectorType::Void
            ? "Void Sector: no atmosphere compartment or systems configured."
            : "Void atmosphere preset: no atmosphere compartment or systems configured.";
        return result;
    }
    station::StationSystemsSettings settings;
    // Station defaults include a global main Power bus. It is not authored by
    // this Sector and must not appear as a fabricated local component.
    settings.powerBuses.clear();
    settings.atmospheres = {*atmosphere};
    const auto graph = station::station_connections::build(settings);
    for (const auto& n : graph.nodes) {
        SectorSystemNode node;
        node.type = n.type; node.id = n.id; node.unresolved = n.unresolved;
        // The catalogue prefixes unresolved presentation types; ports still
        // refer to the canonical component type carried by AuthoredLink.
        if (node.unresolved && node.type.starts_with("Missing ")) node.type.erase(0, 8);
        if (node.unresolved && node.type.starts_with("Ambiguous ")) node.type.erase(0, 10);
        if (node.type == "Atmosphere" && node.id == atmosphere->id) {
            atmosphereFields(node, *atmosphere);
            atmosphereSimulation(node, snapshot);
        }
        else if (n.unresolved) {
            node.configuration.emplace_back("Reference", "Unresolved (not defined in this Installation)");
        } else if (!n.detail.empty()) node.configuration.emplace_back("Details", n.detail);
        for (const auto& descriptor : station::stationPortDescriptors()) {
            if (descriptor.nodeType != node.type) continue;
            node.ports.push_back({{node.type, node.id, std::string(descriptor.name)},
                                  descriptor.direction, descriptor.optional, std::string(descriptor.kind)});
        }
        result.nodes.push_back(std::move(node));
    }
    result.links = station::stationAuthoredLinks(settings);
    return result;
}
} // namespace station::editor
