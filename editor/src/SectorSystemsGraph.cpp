#include "SectorSystemsGraph.h"

#include <nsi/systems/Atmosphere.h>
#include <station/StationConnections.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <set>

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
    if (a.spatial && !a.resolvedGas) node.simulation.emplace_back("Temperature / humidity","Unavailable (unresolved gas)");
    else {field("Temperature",a.temperatureC,"C");field("Relative humidity",a.relativeHumidity*100,"%");}
    field("Pressure", a.pressureKPa, "kPa");
    if (a.spatial) {
        field("Minimum Cell pressure",a.minCellPressureKPa,"kPa");field("Maximum Cell pressure",a.maxCellPressureKPa,"kPa");
        field("Wind kinetic energy",a.kineticEnergyKJ,"kJ");
    }
    const double totalMoles=a.oxygenKg/StationMolarMassOxygenKgPerMol+a.nitrogenKg/StationMolarMassNitrogenKgPerMol
        +a.carbonDioxideKg/StationMolarMassCarbonDioxideKgPerMol+a.waterVapourKg/StationMolarMassWaterKgPerMol;
    auto gas = [&](const char* label, double kg, double partialPressure,double molarMass) {
        const auto fraction = std::isfinite(a.pressureKPa) && a.pressureKPa > 0 && (!a.spatial || a.resolvedGas)
            ? number(a.spatial && totalMoles>0 ? 100*(kg/molarMass)/totalMoles : 100*partialPressure/a.pressureKPa,"%") : "-- %";
        node.simulation.emplace_back(label, number(kg, "kg") + " | " + fraction +
            " | " + number(partialPressure, "kPa"));
    };
    // Spatial mole fractions use inventory, independently of mean partial
    // pressures. CO is retained hazard inventory, outside these four gases.
    node.simulation.emplace_back("Gas composition", "mass | wet mole fraction | partial pressure");
    gas("Oxygen", a.oxygenKg, a.oxygenPartialPressureKPa,StationMolarMassOxygenKgPerMol);
    gas("Nitrogen", a.nitrogenKg, a.nitrogenPartialPressureKPa,StationMolarMassNitrogenKgPerMol);
    gas("Carbon dioxide", a.carbonDioxideKg, a.carbonDioxidePartialPressureKPa,StationMolarMassCarbonDioxideKgPerMol);
    gas("Water vapour", a.waterVapourKg, a.waterVapourPartialPressureKPa,StationMolarMassWaterKgPerMol);
    field("Carbon monoxide", a.carbonMonoxideKg, "kg");
    field("Thermal energy", a.currentEnergyKJ, "kJ");
    field("Current heat capacity",a.thermalCapacityKJPerK,"kJ/K");
    field("Inventory-carried energy",a.inventoryEnergyKJ,"kJ");
    field("Condensed water (total)", a.condensedKg, "kg");
    field("Leaked gas (total)", a.leakedKg, "kg");
    double exterior=0;for (const auto kg:a.exteriorLostKg) exterior+=kg;
    field("Exterior gas loss (total)",exterior,"kg");
    field("Occupant heat", a.lastOccupantHeatKW, "kW");
    field("Reservoir exchange (+ into air)", a.lastExchangeIntoCompartmentKW, "kW");
    field("Conduction (+ into air)", a.lastConductedKW, "kW");
    field("Advection (+ into air)", a.lastAdvectedKW, "kW");
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
    std::set<std::string> neighbours;
    for (const auto& door:nsi::systems::expandDoors(installation)) {
        if (!door.pathway) continue;
        const auto& p=*door.pathway;
        if (p.aAtmosphereId!=atmosphere->id && p.bAtmosphereId!=atmosphere->id) continue;
        settings.atmospherePathways.push_back(p);
        neighbours.insert(p.aAtmosphereId); neighbours.insert(p.bAtmosphereId);
    }
    // Spatial breaches are derived geometry, not serialized Station pathways.
    const auto& grid=installation.level(level);
    auto breach=[&](std::int64_t x,std::int64_t y,std::size_t wall,double area) {
        if (grid.walls()[wall].type!=nsi::core::Installation::WallType::None) return;
        if (x>=0 && y>=0 && x<grid.width() && y<grid.depth()
            && grid.cell(x,y)!=nsi::core::Installation::SectorType::Void) return;
        station::AtmospherePathwaySettings p;p.id="breach-"+std::to_string(level)+"-"+std::to_string(wall);
        p.aAtmosphereId=atmosphere->id;p.bAtmosphereId=StationExteriorAtmosphereId;p.areaM2=area;p.openFraction=1;p.dischargeCoefficient=1;
        settings.atmospherePathways.push_back(std::move(p));neighbours.insert(StationExteriorAtmosphereId);
    };
    for (std::size_t i=0;i<grid.cellCount();++i) if (grid.cells()[i].sector==int(sector)) {
        const auto x=std::int64_t(i%grid.width()),y=std::int64_t(i/grid.width());
        breach(x-1,y,grid.verticalWall(x,y),nsi::core::CellDepthM*nsi::core::LevelHeightM);
        breach(x+1,y,grid.verticalWall(x+1,y),nsi::core::CellDepthM*nsi::core::LevelHeightM);
        breach(x,y-1,grid.horizontalWall(x,y),nsi::core::CellWidthM*nsi::core::LevelHeightM);
        breach(x,y+1,grid.horizontalWall(x,y+1),nsi::core::CellWidthM*nsi::core::LevelHeightM);
    }
    // LocalStructure's real sides/contacts live in the bounded structural view.
    // Keep authored gas/devices here without fabricating legacy geometry links.
    const auto allAtmospheres=nsi::systems::expandInstallation(installation);
    for (auto neighbour:allAtmospheres) {
        if (neighbour.id==atmosphere->id || !neighbours.contains(neighbour.id)) continue;
        // Opposite endpoints are context, not roots of a recursive traversal.
        neighbour.reservoirId.clear(); neighbour.exchangerConductanceKWPerK=0;
        settings.atmospheres.push_back(std::move(neighbour));
    }
    auto graph = station::station_connections::build(settings);
    result.links=station::stationAuthoredLinks(settings);
    // The shared editable catalogue deliberately omits exterior hatches. This
    // canvas is read-only: expose their actual endpoints as display-only links.
    for (const auto& p:settings.atmospherePathways) {
        if (p.aAtmosphereId!=StationExteriorAtmosphereId && p.bAtmosphereId!=StationExteriorAtmosphereId) continue;
        graph.nodes.push_back({"Atmosphere pathway",p.id,{},2,false});
        auto end=[&](const std::string& id,const char* port) {
            const bool exterior=id==StationExteriorAtmosphereId;
            result.links.push_back({"display:"+p.id+":"+port,
                {exterior ? "Exterior" : "Atmosphere",id,exterior ? "Boundary" : "Hosted pathways"},
                {"Atmosphere pathway",p.id,port}});
        };
        end(p.aAtmosphereId,"End A"); end(p.bAtmosphereId,"End B");
    }
    auto liveStatus=[&](SectorSystemNode& node,bool found) {
        node.simulationStatus=found
            ? "As of "+number(snapshot->elapsedSeconds,"s")+(snapshot->healthy ? "" : " (last committed state; simulation failed)")
            : "Unavailable: no matching simulation component.";
    };
    for (const auto& n : graph.nodes) {
        SectorSystemNode node;
        node.type = n.type; node.id = n.id; node.unresolved = n.unresolved;
        // The catalogue prefixes unresolved presentation types; ports still
        // refer to the canonical component type carried by AuthoredLink.
        if (node.unresolved && node.type.starts_with("Missing ")) node.type.erase(0, 8);
        if (node.unresolved && node.type.starts_with("Ambiguous ")) node.type.erase(0, 10);
        const auto air=std::find_if(settings.atmospheres.begin(),settings.atmospheres.end(),
            [&](const auto& a){ return a.id==node.id; });
        if (node.type == "Atmosphere" && air!=settings.atmospheres.end()) {
            // Use the original assignment for neighbour readouts, even though
            // its outgoing references were excluded from this local subgraph.
            const auto original=std::find_if(allAtmospheres.begin(),allAtmospheres.end(),[&](const auto& a){return a.id==node.id;});
            atmosphereFields(node,original!=allAtmospheres.end() ? *original : *air);
            atmosphereSimulation(node, snapshot);
        }
        else if (node.type=="Atmosphere pathway") {
            const auto& p=*std::find_if(settings.atmospherePathways.begin(),settings.atmospherePathways.end(),
                [&](const auto& p){return p.id==node.id;});
            node.configuration={{"End A",p.aAtmosphereId},{"End B",p.bAtmosphereId},
                {"Opening area",number(p.areaM2,"m2")},{"Starting state","Closed"},
                {"Discharge coefficient",number(p.dischargeCoefficient)}};
            const StationAtmospherePathwaySnapshot* live=nullptr;
            if (snapshot && snapshot->enabled)
                for (const auto& candidate:snapshot->atmospherePathways) if (candidate.id==node.id) live=&candidate;
            liveStatus(node,live!=nullptr);
            if (live) {
                const double sign=p.aAtmosphereId==atmosphere->id ? 1 : -1;
                node.simulation.emplace_back("State",live->openFraction>0 ? "Open" : "Closed");
                node.simulation.emplace_back("Open fraction",number(live->openFraction));
                const char* regime=live->spatialApproximation ? "Balanced local pressures" : "No bulk flow";
                switch (live->flowRegime) {
                    case StationAtmosphereFlowRegime::Sealed: regime="Sealed"; break;
                    case StationAtmosphereFlowRegime::Subsonic: regime="Subsonic"; break;
                    case StationAtmosphereFlowRegime::Choked: regime="Choked"; break;
                    case StationAtmosphereFlowRegime::NoBulkFlow: break;
                }
                node.simulation.emplace_back("Flow regime",regime);
                node.simulation.emplace_back("Downstream / upstream pressure",number(live->pressureRatio));
                node.simulation.emplace_back("Bulk gas flow (outward)",number(sign*live->bulkFlowRateKgPerS,"kg/s"));
                node.simulation.emplace_back("Regime convention",live->spatialApproximation
                    ? "Cell-face flux; pressure-based regime estimate, not an analytic nozzle solve"
                    : "Current state; flow rates are last-step averages");
                const char* gases[]={"Oxygen flow (outward)","Nitrogen flow (outward)","Carbon dioxide flow (outward)","Water vapour flow (outward)"};
                for (std::size_t gas=0;gas<4;++gas)
                    node.simulation.emplace_back(gases[gas],number(sign*live->flowRateKgPerS[gas],"kg/s"));
                node.simulation.emplace_back("Advected heat (outward)",number(sign*live->lastAdvectedKW,"kW"));
                node.simulation.emplace_back("Flow convention","Last-step averages; positive leaves selected Sector");
            }
        }
        else if (n.unresolved) {
            node.configuration.emplace_back("Reference", "Unresolved (not defined in this Installation)");
        } else if (!n.detail.empty()) node.configuration.emplace_back("Details", n.detail);
        for (const auto& descriptor : station::stationPortDescriptors()) {
            if (descriptor.nodeType != node.type) continue;
            node.ports.push_back({{node.type, node.id, std::string(descriptor.name)},
                                  descriptor.direction, descriptor.optional, std::string(descriptor.kind)});
        }
        // Derived boundary nodes have no editable catalogue ports. Their
        // display-only connectors make real boundary crossings visible here.
        if (node.type=="Exterior")
            node.ports.push_back({{"Exterior",node.id,"Boundary"},StationPortDirection::Output,false,"pathway-host"});
        result.nodes.push_back(std::move(node));
    }
    return result;
}
} // namespace station::editor
