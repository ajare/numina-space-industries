#include "CellPlaybackRuntime.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <utility>

namespace station::editor {
namespace {
constexpr std::array<double,4> inverseMolarMass{1/StationMolarMassOxygenKgPerMol,1/StationMolarMassNitrogenKgPerMol,
    1/StationMolarMassCarbonDioxideKgPerMol,1/StationMolarMassWaterKgPerMol};
StationSystemsSettings lifecycleSettings(const nsi::systems::CellAtmosphereExpansion& geometry) {
    StationSystemsSettings settings;settings.enabled=true;settings.atmospheres=geometry.atmospheres;
    settings.solarCycle=geometry.thermal.solarCycle;return settings;
}
}
CellPlaybackRuntime::CellPlaybackRuntime(const nsi::core::Installation& installation,CellObservationOptions options,StationSystemsSource source)
    :model_(installation,nsi::systems::CellThermalMode::Geometry),lifecycle_(lifecycleSettings(model_.geometry()),std::move(source)),
    aerodynamics_(model_.fluid().states().size()),options_(options),previous_(model_.sectors().size()) {
    diagnostics_.reserve(1);snapshot_.enabled=true;snapshot_.variableTimestep=true;snapshot_.systemsStepWorldTicks=0;
    const auto& geometry=model_.geometry();snapshot_.atmospheres.resize(geometry.atmospheres.size());
    for (std::size_t i=0;i<geometry.atmospheres.size();++i) {
        const auto& a=geometry.atmospheres[i];auto& s=snapshot_.atmospheres[i];
        s.id=a.id;s.reservoirId=a.reservoirId;s.volumeM3=a.volumeM3;s.exchangerConductanceKWPerK=a.exchangerConductanceKWPerK;
        s.occupants=a.occupants;s.oxygenUsePerOccupantKgPerS=a.oxygenUsePerOccupantKgPerS;
        s.co2ProductionPerOccupantKgPerS=a.co2ProductionPerOccupantKgPerS;s.sensibleHeatPerOccupantKW=a.sensibleHeatPerOccupantKW;
        s.moistureAdditionPerOccupantKgPerS=a.moistureAdditionPerOccupantKgPerS;s.leakRateKgPerS=a.leakRateKgPerS;
        s.minPressureKPa=a.minPressureKPa;s.maxPressureKPa=a.maxPressureKPa;
        s.minOxygenPartialPressureKPa=a.minOxygenPartialPressureKPa;s.maxCarbonDioxidePartialPressureKPa=a.maxCarbonDioxidePartialPressureKPa;
        s.spatial=true;s.inventoryThermodynamics=true;s.thermallyCoupled=std::any_of(geometry.thermal.links.begin(),geometry.thermal.links.end(),[&](const auto& link){return link.aAtmosphereId==a.id || link.bAtmosphereId==a.id;});
    }
    snapshot_.cellAtmospheres.resize(model_.fluid().states().size());
    for (std::size_t i=0;i<snapshot_.cellAtmospheres.size();++i) {
        const auto& c=model_.fluid().mesh().cells[i];const auto& grid=installation.level(c.level);auto& s=snapshot_.cellAtmospheres[i];
        s.level=c.level;s.gridIndex=c.gridIndex;s.sectorUid=grid.sectorAt(c.gridIndex%grid.width(),c.gridIndex/grid.width()).uid;
    }
    for (const auto& link:geometry.thermal.links) {
        StationThermalLinkSnapshot s;s.id=link.id;s.aAtmosphereId=link.aAtmosphereId;s.bAtmosphereId=link.bAtmosphereId;
        s.aKind=link.aKind;s.bKind=link.bKind;s.conductanceKWPerK=link.conductanceKWPerK;snapshot_.thermalLinks.push_back(std::move(s));
    }
    for (const auto& d:nsi::systems::expandDoors(installation)) if (d.pathway) {
        const auto it=std::find_if(geometry.doors.begin(),geometry.doors.end(),[&](const auto& x){return x.level==d.level && x.wall==d.wall;});
        if (it==geometry.doors.end() || it->face==SIZE_MAX) continue;
        const auto& f=model_.fluid().mesh().faces[it->face];const auto& path=*d.pathway;
        const double sign=geometry.atmospheres[model_.fluid().mesh().cells[f.a].atmosphere].id==path.aAtmosphereId ? 1 : -1;
        StationAtmospherePathwaySnapshot s;s.id=path.id;s.aAtmosphereId=path.aAtmosphereId;s.bAtmosphereId=path.bAtmosphereId;
        s.areaM2=path.areaM2;s.dischargeCoefficient=f.dischargeCoefficient;s.spatialApproximation=true;
        snapshot_.atmospherePathways.push_back(std::move(s));paths_.push_back({it->face,sign});
    }
    for (std::size_t i=0;i<model_.fluid().mesh().faces.size();++i) {
        const auto& f=model_.fluid().mesh().faces[i];if (f.b!=SIZE_MAX || f.openFraction==0) continue;
        const auto& c=model_.fluid().mesh().cells[f.a];const auto& grid=installation.level(c.level);
        const auto x=c.gridIndex%grid.width(),y=c.gridIndex/grid.width();
        const auto wall=f.axis==0 ? grid.verticalWall(x+(f.sign>0 ? 1 : 0),y) : grid.horizontalWall(x,y+(f.sign>0 ? 1 : 0));
        StationAtmospherePathwaySnapshot s;s.id="breach-"+std::to_string(c.level)+"-"+std::to_string(wall);
        s.aAtmosphereId=geometry.atmospheres[c.atmosphere].id;s.bAtmosphereId=StationExteriorAtmosphereId;
        s.areaM2=f.areaM2;s.dischargeCoefficient=f.dischargeCoefficient;s.spatialApproximation=true;
        snapshot_.atmospherePathways.push_back(std::move(s));paths_.push_back({i,1});
    }
    publish(0);
    for (auto& s:snapshot_.atmospheres) {
        s.startingOxygenKg=s.oxygenKg;s.startingNitrogenKg=s.nitrogenKg;s.startingCarbonDioxideKg=s.carbonDioxideKg;
        s.startingWaterVapourKg=s.waterVapourKg;s.startingTemperatureC=s.temperatureC;s.startingPressureKPa=s.pressureKPa;
        s.startingThermalCapacityKJPerK=s.thermalCapacityKJPerK;s.startingEnergyKJ=s.currentEnergyKJ;
        s.residualKJ=0;
    }
}
bool CellPlaybackRuntime::update(double seconds,std::size_t acousticBudget,std::size_t thermalBudget) {
    if (seconds==0) return true;
    diagnostics_.clear();
    for (std::size_t i=0;i<previous_.size();++i) previous_[i]=model_.sectors()[i].ledger;
    const auto start=std::chrono::steady_clock::now();
    if (!lifecycle_.prepare(seconds)) {
        snapshot_.healthy=false;diagnostics_=lifecycle_.diagnostics();return false;
    }
    const auto luaEnd=std::chrono::steady_clock::now();
    if (!model_.advance(seconds,{},acousticBudget,thermalBudget)) {
        snapshot_.healthy=false;diagnostics_.push_back({snapshot_.worldTick,"native-cell-fluid",ScriptExecutionFailure::None,model_.diagnostic()});return false;
    }
    const auto solverEnd=std::chrono::steady_clock::now();
    lifecycle_.commit();snapshot_.worldTick=lifecycle_.completedSteps();snapshot_.completedTicks=lifecycle_.completedTicks();
    snapshot_.systemsTimestepSeconds=seconds;snapshot_.elapsedSeconds=model_.elapsedSeconds();snapshot_.healthy=true;
    publish(seconds);
    timing_.recordMicroseconds(std::chrono::duration<double,std::micro>(solverEnd-luaEnd).count(),
        std::chrono::duration<double,std::micro>(luaEnd-start).count());return true;
}
bool CellPlaybackRuntime::setDoorOpen(std::size_t level,std::size_t wall,bool open) {
    if (!model_.setDoorOpen(level,wall,open)) return false;
    doorConditions();publishDiagnostics();return true;
}
void CellPlaybackRuntime::setObservationOptions(CellObservationOptions options) {
    options_=options;publishDiagnostics();
}
std::optional<CellAerodynamicLoad> CellPlaybackRuntime::objectForce(std::size_t level,std::size_t gridIndex,const CellAerodynamicBody& body) const {
    if (!options_.objectForces || level>=model_.geometry().cellIndices.size()) return std::nullopt;
    const auto& cells=model_.geometry().cellIndices[level];if (gridIndex>=cells.size() || cells[gridIndex]==SIZE_MAX) return std::nullopt;
    const auto load=aerodynamics_.sample(model_.fluid(),cells[gridIndex],body);
    return load.available ? std::optional<CellAerodynamicLoad>(load) : std::nullopt;
}
void CellPlaybackRuntime::publishDiagnostics() {
    if (options_.objectForces) aerodynamics_.update(model_.fluid());
    for (std::size_t i=0;i<snapshot_.cellAtmospheres.size();++i) {
        auto& s=snapshot_.cellAtmospheres[i];const auto& p=model_.fluid().primitives()[i];
        s.densityKgPerM3=p.density;s.windEnabled=options_.windDiagnostics;s.forcesEnabled=options_.objectForces;
        s.windAvailable=s.windEnabled && p.resolvedGas;s.forceAvailable=false;
        s.velocityX=s.windAvailable ? p.velocityX : 0;s.velocityY=s.windAvailable ? p.velocityY : 0;
        s.windSpeedMS=s.windAvailable ? std::hypot(p.velocityX,p.velocityY) : 0;
        if (!std::isfinite(s.windSpeedMS)) {s.windAvailable=false;s.windSpeedMS=s.velocityX=s.velocityY=0;}
        s.dynamicPressurePa=0;s.pressureGradientPaPerM=s.dragForceN=s.pressureForceN=s.forceN={};
        if (!options_.objectForces) continue;
        const auto force=aerodynamics_.sample(model_.fluid(),i);
        s.forceAvailable=force.available;
        if (force.available) {
            s.dynamicPressurePa=force.dynamicPressurePa;s.pressureGradientPaPerM=aerodynamics_.pressureGradients()[i];
            s.dragForceN=force.dragN;s.pressureForceN=force.pressureN;s.forceN=force.forceN;
        }
    }
}
void CellPlaybackRuntime::doorConditions() {
    for (std::size_t i=0;i<paths_.size();++i) {
        auto& s=snapshot_.atmospherePathways[i];const auto& f=model_.fluid().mesh().faces[paths_[i].face];
        s.openFraction=f.openFraction>0 ? 1 : 0;
        const auto& a=model_.fluid().primitives()[f.a];const auto* b=f.b==SIZE_MAX ? nullptr : &model_.fluid().primitives()[f.b];
        const double pa=a.pressurePa,pb=b ? b->pressurePa : 0,up=std::max(pa,pb);
        s.pressureRatio=up>0 ? std::min(pa,pb)/up : 1;
        s.flowRegime=StationAtmosphereFlowRegime::Sealed;
        if (s.openFraction>0) {
            s.flowRegime=StationAtmosphereFlowRegime::NoBulkFlow;
            if (up>0 && std::abs(pa-pb)>1e-9*up) {
                const auto& donor=pa>=pb ? a : *b;
                const double gamma=donor.pressurePa>0 ? donor.soundSpeed*donor.soundSpeed*donor.density/donor.pressurePa : 1.4;
                const double critical=gamma>1 ? std::pow(2/(gamma+1),gamma/(gamma-1)) : 0;
                s.flowRegime=s.pressureRatio<=critical ? StationAtmosphereFlowRegime::Choked : StationAtmosphereFlowRegime::Subsonic;
            }
        }
    }
}
void CellPlaybackRuntime::publish(double seconds) {
    const auto& geometry=model_.geometry();
    for (std::size_t i=0;i<model_.sectors().size();++i) {
        const auto& a=model_.sectors()[i];const auto& l=a.ledger;const auto& old=previous_[i];auto& s=snapshot_.atmospheres[i];
        s.oxygenKg=a.massKg[0];s.nitrogenKg=a.massKg[1];s.carbonDioxideKg=a.massKg[2];s.waterVapourKg=a.massKg[3];
        s.resolvedGas=a.resolvedGas;s.temperatureC=a.capacityJPerK>0 ? a.temperatureK-StationCelsiusOffset : 0;
        s.thermalCapacityKJPerK=a.capacityJPerK/1000;s.currentEnergyKJ=(a.totalEnergyJ-a.kineticEnergyJ)/1000;s.kineticEnergyKJ=a.kineticEnergyJ/1000;
        s.pressureKPa=a.meanPressurePa/1000;s.minCellPressureKPa=a.minPressurePa/1000;s.maxCellPressureKPa=a.maxPressurePa/1000;
        s.oxygenPartialPressureKPa=s.nitrogenPartialPressureKPa=s.carbonDioxidePartialPressureKPa=s.waterVapourPartialPressureKPa=0;
        s.relativeHumidity=s.saturationVapourPressureKPa=0;s.lowOxygen=s.highCarbonDioxide=false;
        s.oxygenConsumedKg=l.oxygenConsumedKg;s.co2ProducedKg=l.co2ProducedKg;s.vapourAddedKg=l.vapourAddedKg;s.condensedKg=l.condensedKg;
        s.leakedKg=std::accumulate(l.leakedKg.begin(),l.leakedKg.end(),0.0);s.exteriorLostKg=l.exteriorLostKg;
        s.inventoryEnergyKJ=l.inventoryEnergyJ/1000;s.occupantHeatEnergyKJ=l.occupantHeatJ/1000;s.latentHeatEnergyKJ=l.latentHeatJ/1000;
        s.exchangedEnergyKJ=l.externalHeatJ/1000;s.conductedEnergyKJ=l.conductedHeatJ/1000;s.advectedEnergyKJ=l.transportedEnergyJ/1000;
        s.residualKJ=s.startingEnergyKJ+s.inventoryEnergyKJ+s.occupantHeatEnergyKJ+s.latentHeatEnergyKJ+s.exchangedEnergyKJ
            +s.conductedEnergyKJ+s.advectedEnergyKJ-s.currentEnergyKJ-s.kineticEnergyKJ;
        if (seconds>0) {
            s.lastOxygenDemandKg=l.oxygenDemandKg-old.oxygenDemandKg;s.lastOxygenConsumedKg=l.oxygenConsumedKg-old.oxygenConsumedKg;
            s.lastOxygenUnmetKg=s.lastOxygenDemandKg-s.lastOxygenConsumedKg;s.lastCo2ProducedKg=l.co2ProducedKg-old.co2ProducedKg;
            s.lastVapourAddedKg=l.vapourAddedKg-old.vapourAddedKg;s.lastVapourCondensedKg=l.condensedKg-old.condensedKg;
            s.lastLeakedKg=s.leakedKg-std::accumulate(old.leakedKg.begin(),old.leakedKg.end(),0.0);
            s.lastOccupantHeatKW=(l.occupantHeatJ-old.occupantHeatJ)/(1000*seconds);s.lastLatentHeatKW=(l.latentHeatJ-old.latentHeatJ)/(1000*seconds);
            s.lastConductedKW=(l.conductedHeatJ-old.conductedHeatJ)/(1000*seconds);
            s.lastAdvectedKW=(l.transportedEnergyJ-old.transportedEnergyJ)/(1000*seconds);
            s.lastExchangeIntoCompartmentKW=(l.externalHeatJ-old.externalHeatJ)/(1000*seconds);
        }
        s.condensing=s.lastVapourCondensedKg>0;s.status=seconds==0 ? StationTransferStatus::Idle : s.lastOxygenUnmetKg>1e-12
            ? (s.lastOxygenConsumedKg>0 ? StationTransferStatus::Partial : StationTransferStatus::Rejected) : StationTransferStatus::Full;
    }
    for (std::size_t i=0;i<snapshot_.cellAtmospheres.size();++i) {
        auto& s=snapshot_.cellAtmospheres[i];const auto& c=model_.fluid().mesh().cells[i];const auto& p=model_.fluid().primitives()[i];
        const auto& q=model_.fluid().states()[i].q;auto& a=snapshot_.atmospheres[c.atmosphere];
        for (int g=0;g<4;++g) s.massKg[g]=q[g];
        s.resolvedGas=p.resolvedGas;s.pressureKPa=p.pressurePa/1000;s.temperatureC=p.resolvedGas ? p.temperatureK-StationCelsiusOffset : 0;
        s.relativeHumidity=0;
        if (!s.resolvedGas) {a.lowOxygen=a.minOxygenPartialPressureKPa>0 || a.lowOxygen;continue;}
        std::array<double,4> moles{};double total=0;
        for (int g=0;g<4;++g) {moles[g]=q[g]*inverseMolarMass[g];total+=moles[g];}
        if (!(total>0)) continue;
        std::array<double,4> partial{};for (int g=0;g<4;++g) partial[g]=s.pressureKPa*moles[g]/total;
        const double sat=stationSaturationVapourPressureKPa(s.temperatureC),weight=c.volumeM3/a.volumeM3;
        s.relativeHumidity=sat>0 ? partial[3]/sat : 0;
        a.lowOxygen=a.lowOxygen || (a.minOxygenPartialPressureKPa>0 && partial[0]<a.minOxygenPartialPressureKPa);
        a.highCarbonDioxide=a.highCarbonDioxide || (a.maxCarbonDioxidePartialPressureKPa>0 && partial[2]>a.maxCarbonDioxidePartialPressureKPa);
        a.relativeHumidity+=s.relativeHumidity*weight;a.saturationVapourPressureKPa+=sat*weight;
        a.oxygenPartialPressureKPa+=partial[0]*weight;a.nitrogenPartialPressureKPa+=partial[1]*weight;
        a.carbonDioxidePartialPressureKPa+=partial[2]*weight;a.waterVapourPartialPressureKPa+=partial[3]*weight;
    }
    for (auto& a:snapshot_.atmospheres) {
        a.belowPressureLimit=a.minCellPressureKPa<a.minPressureKPa;a.abovePressureLimit=a.maxCellPressureKPa>a.maxPressureKPa;
    }
    for (std::size_t i=0;i<snapshot_.thermalLinks.size();++i) {
        auto& s=snapshot_.thermalLinks[i];const double energy=model_.thermal().linkEnergyJ[i]/1000;
        s.flowRateKW=seconds>0 ? (energy-s.netTransferredKJ)/seconds : 0;s.netTransferredKJ=energy;
        s.status=s.flowRateKW!=0 ? StationThermalLinkStatus::Active : StationThermalLinkStatus::Idle;
    }
    if (model_.thermal().hull) {
        const auto& h=*model_.thermal().hull;const auto& cfg=*geometry.thermal.hull;
        const auto previous=snapshot_.thermal.hull.value_or(StationHullSnapshot{});StationHullSnapshot s;
        s.temperatureC=static_cast<double>(h.temperatureK-StationCelsiusOffset);
        s.startingEnergyKJ=cfg.heatCapacityKJPerK*cfg.startingTemperatureC;s.currentEnergyKJ=cfg.heatCapacityKJPerK*s.temperatureC;
        s.absorbedEnergyKJ=h.absorbedJ/1000;s.emittedEnergyKJ=h.emittedJ/1000;s.conductedKJ=h.conductedJ/1000;
        if (seconds>0) {s.absorbedKW=(s.absorbedEnergyKJ-previous.absorbedEnergyKJ)/seconds;
            s.emittedKW=(s.emittedEnergyKJ-previous.emittedEnergyKJ)/seconds;s.conductedKW=(s.conductedKJ-previous.conductedKJ)/seconds;}
        s.residualKJ=s.startingEnergyKJ+s.absorbedEnergyKJ-s.emittedEnergyKJ+s.conductedKJ-s.currentEnergyKJ;
        s.belowMin=s.temperatureC<cfg.minTemperatureC;s.aboveMax=s.temperatureC>cfg.maxTemperatureC;snapshot_.thermal.hull=s;
        snapshot_.thermal.totalAbsorbedKW=s.absorbedKW;snapshot_.thermal.totalRejectedKW=s.emittedKW;
    }
    for (std::size_t i=0;i<paths_.size();++i) {
        auto& s=snapshot_.atmospherePathways[i];const auto& binding=paths_[i];const auto& q=model_.faceTransfers()[binding.face].q;
        s.bulkFlowRateKgPerS=0;
        for (int g=0;g<4;++g) {
            const double kg=seconds>0 ? binding.sign*q[g] : 0;s.netTransferredKg[g]+=kg;
            s.flowRateKgPerS[g]=seconds>0 ? kg/seconds : 0;s.bulkFlowRateKgPerS+=s.flowRateKgPerS[g];
        }
        const double energy=seconds>0 ? binding.sign*q[6]/1000 : 0;s.advectedKJ+=energy;s.lastAdvectedKW=seconds>0 ? energy/seconds : 0;
        s.status=s.bulkFlowRateKgPerS!=0 ? StationAtmospherePathwayStatus::Active : StationAtmospherePathwayStatus::Idle;
    }
    if (geometry.thermal.solarCycle) snapshot_.solarCycle=evaluateStationProfile(*geometry.thermal.solarCycle,snapshot_.elapsedSeconds);
    doorConditions();publishDiagnostics();
}
} // namespace station::editor
