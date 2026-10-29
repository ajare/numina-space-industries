#include "../src/StationGasFlow.h"
#include "station/StationSystemsRuntime.h"
#include "station/Persistence.h"
#include "station/Replay.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace station;
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a,double b,double relative=1e-8) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a-b)>relative*std::max(1.0,std::abs(b)))
        throw std::runtime_error("Numeric gas-flow reference mismatch: "+std::to_string(a)+" vs "+std::to_string(b));
}
GasVolume nitrogen(double pressurePa,double volume=144,double temperature=293.15) {
    GasVolume v; v.volumeM3=volume; v.temperatureK=temperature;
    v.mass[1]=pressurePa*volume*StationMolarMassNitrogenKgPerMol/(StationUniversalGasConstantJPerMolK*temperature);
    return v;
}
double energy(const GasVolume& v) { return gasMixture(v.mass).capacityKJPerK*v.temperatureK; }
void laws() {
    const auto a=nitrogen(101325), low=nitrogen(10000), high=nitrogen(95000);
    const auto closed=compressibleGasFlow(&a,nullptr,0,0.7);
    require(closed.regime==StationAtmosphereFlowRegime::Sealed && closed.massKgPerS==0,"Sealed opening leaked");
    const auto vent=compressibleGasFlow(&a,nullptr,6,0.7);
    const auto choked=compressibleGasFlow(&a,&low,6,0.7);
    const auto subsonic=compressibleGasFlow(&a,&high,6,0.7);
    require(vent.regime==StationAtmosphereFlowRegime::Choked && choked.regime==vent.regime,"Vacuum/low pressure not choked");
    require(subsonic.regime==StationAtmosphereFlowRegime::Subsonic && subsonic.massKgPerS<choked.massKgPerS,"Subsonic flow wrong");
    near(vent.massKgPerS,choked.massKgPerS);
    near(compressibleGasFlow(nullptr,&a,6,0.7).massKgPerS,-vent.massKgPerS);
    near(compressibleGasFlow(&a,nullptr,3,0.7).massKgPerS,vent.massKgPerS/2);
    near(compressibleGasFlow(&a,nullptr,6,0.35).massKgPerS,vent.massKgPerS/2);
    require(compressibleGasFlow(&a,&a,6,0.7).regime==StationAtmosphereFlowRegime::NoBulkFlow,"Equal pressure created bulk flow");
    const auto m=gasMixture(a.mass); const double gamma=m.gamma();
    const double critical=std::pow(2/(gamma+1),gamma/(gamma-1));
    near(critical,0.528,0.002);
    const auto left=nitrogen(101325*(critical-1e-7)),right=nitrogen(101325*(critical+1e-7));
    near(compressibleGasFlow(&a,&left,6,0.7).massKgPerS,compressibleGasFlow(&a,&right,6,0.7).massKgPerS,1e-6);
    // Analytical dry adiabatic choked blowdown of a finite well-mixed vessel.
    std::vector<GasVolume> v{a};
    const auto moved=integrateGasOpenings(v,{{0,SIZE_MAX,6,0.7}},0.2);
    const double expectedMass=m.massKg*std::pow(1+(gamma-1)/2*vent.massKgPerS/m.massKg*0.2,-2/(gamma-1));
    near(gasMixture(v[0].mass).massKg,expectedMass,0.01);
    near(v[0].temperatureK,a.temperatureK*std::pow(gasMixture(v[0].mass).massKg/m.massKg,gamma-1),0.01);
    near(energy(a),energy(v[0])+moved[0].energyKJ);
    auto whole=std::vector<GasVolume>{a},split=whole;
    integrateGasOpenings(whole,{{0,SIZE_MAX,6,0.7}},1);
    for (int n=0;n<100;++n) integrateGasOpenings(split,{{0,SIZE_MAX,6,0.7}},0.01);
    near(gasPressurePa(whole[0]),gasPressurePa(split[0]),0.005);
    require(gasPressurePa(whole[0])<2000,"Large vacuum opening still has diffusion timescale");
    // Same pressure/temperature/opening has the same initial kg/s, not the
    // same pressure-decay time. Inventory and blowdown time scale with V/A.
    const double volumeRatio=492.0/144.0;
    auto small=std::vector<GasVolume>{nitrogen(101325,144)};
    auto large=std::vector<GasVolume>{nitrogen(101325,492)};
    auto smallerOpening=small;
    near(compressibleGasFlow(&small[0],nullptr,6,0.7).massKgPerS,
         compressibleGasFlow(&large[0],nullptr,6,0.7).massKgPerS);
    integrateGasOpenings(small,{{0,SIZE_MAX,6,0.7}},0.2);
    integrateGasOpenings(large,{{0,SIZE_MAX,6,0.7}},0.2*volumeRatio);
    integrateGasOpenings(smallerOpening,{{0,SIZE_MAX,1.8,0.7}},0.2*6/1.8);
    // Match the 1% numerical accuracy checked against analytical blowdown;
    // changing V/A also changes which adaptive substep bound is active.
    near(gasPressurePa(large[0]),gasPressurePa(small[0]),0.01);
    near(gasPressurePa(smallerOpening[0]),gasPressurePa(small[0]),0.01);
    near(gasMixture(large[0].mass).massKg,gasMixture(small[0].mass).massKg*volumeRatio,0.01);
}
void conservation() {
    std::vector<GasVolume> v{nitrogen(101325),nitrogen(1000),nitrogen(1000)};
    const auto before=v; double e=0,mass=0;
    for (const auto& a:v) {e+=energy(a);mass+=gasMixture(a.mass).massKg;}
    integrateGasOpenings(v,{{0,1,6,0.7},{0,2,6,0.7}},2);
    double afterE=0,afterMass=0;
    for (const auto& a:v) {
        afterE+=energy(a);afterMass+=gasMixture(a.mass).massKg;
        require(a.temperatureK>0 && std::all_of(a.mass.begin(),a.mass.end(),[](double m){return m>=0;}),"Network made negative gas or absolute temperature");
    }
    near(afterE,e);near(afterMass,mass);
    near(gasPressurePa(v[0]),gasPressurePa(v[1]),1e-5);
    // At equal pressure, different species still counterdiffuse.
    GasVolume oxygen=nitrogen(101325); oxygen.mass[0]=oxygen.mass[1]*StationMolarMassOxygenKgPerMol/StationMolarMassNitrogenKgPerMol; oxygen.mass[1]=0;
    v={nitrogen(101325),oxygen};e=energy(v[0])+energy(v[1]);
    const auto mixed=integrateGasOpenings(v,{{0,1,1,0.7}},1);
    require(mixed[0].massKg[1]>0 && mixed[0].massKg[0]<0,"Equal-pressure composition mixing was lost");
    near(energy(v[0])+energy(v[1]),e);
}
void runtime() {
    auto volume=nitrogen(101325);
    StationSystemsSettings s;s.enabled=true;s.systemsStepWorldTicks=1;
    AtmosphereSettings a;a.id="room";a.volumeM3=volume.volumeM3;a.startingTemperatureC=20;
    a.startingNitrogenKg=volume.mass[1];a.minPressureKPa=1;a.maxPressureKPa=200;
    a.thermalCapacityKJPerK=10000;
    s.atmospheres={a};s.atmospherePathways={{"door","room",StationExteriorAtmosphereId,6,0}};
    StationSystemsRuntime r(s,StationSystemsUpdateMode::Variable);
    auto start=r.snapshot();
    near(start.atmospheres[0].thermalCapacityKJPerK,gasMixture(volume.mass).capacityKJPerK);
    require(start.atmospherePathways[0].flowRegime==StationAtmosphereFlowRegime::Sealed,"Closed runtime diagnostic wrong");
    std::string why;
    require(r.updateNode<AtmospherePathwaySettings>("door",[](auto& p){p.openFraction=1;},&why),why.c_str());
    require(r.snapshot().atmospherePathways[0].flowRegime==StationAtmosphereFlowRegime::Choked,"Opening diagnostic delayed until tick");
    for (int n=0;n<100;++n) require(r.update(0.01),"Runtime compressible step failed audit");
    const auto end=r.snapshot();
    require(end.healthy && end.atmospheres[0].pressureKPa<2,"Runtime decompression too slow");
    require(end.atmospheres[0].thermalCapacityKJPerK<start.atmospheres[0].thermalCapacityKJPerK,"Gas capacity did not follow inventory");
    near(end.atmospheres[0].residualKJ,0,1e-6);
    require(r.updateNode<AtmospherePathwaySettings>("door",[](auto& p){p.dischargeCoefficient=0.41;},&why),why.c_str());
    require(r.update(0.01),"Live discharge coefficient update failed");
    near(r.snapshot().atmospherePathways[0].dischargeCoefficient,0.41);
    std::unique_ptr<StationSystemsRuntime> replay;
    require(parseStationReplay(serializeStationReplay(r),replay,&why),why.c_str());
    require(replay->snapshot().atmospheres==r.snapshot().atmospheres
        && replay->snapshot().atmospherePathways==r.snapshot().atmospherePathways,"Compressible replay did not preserve gas/energy state");
    auto retuned=s;retuned.atmospheres[0].thermalCapacityKJPerK=1;
    retuned.atmospheres[0].advectedSpecificHeatKJPerKgK=4;
    retuned.atmospherePathways[0].openFraction=1;
    StationSystemsRuntime noHiddenWall(retuned,StationSystemsUpdateMode::Variable);
    for (int n=0;n<100;++n) require(noHiddenWall.update(0.01),"Gas-only capacity run failed");
    near(noHiddenWall.snapshot().atmospheres[0].pressureKPa,end.atmospheres[0].pressureKPa);
    // Existing non-pathway mass sources/sinks retain their own sensible-energy
    // account rather than getting silently folded into Door enthalpy.
    auto loadedScene=s;auto& occupant=loadedScene.atmospheres[0];
    occupant.startingOxygenKg=25;occupant.startingWaterVapourKg=0.3;occupant.occupants=1;
    occupant.oxygenUsePerOccupantKgPerS=0.01;occupant.co2ProductionPerOccupantKgPerS=0.01;
    occupant.moistureAdditionPerOccupantKgPerS=0.0001;occupant.sensibleHeatPerOccupantKW=0.1;occupant.leakRateKgPerS=0.001;
    StationSystemsRuntime occupied(loadedScene,StationSystemsUpdateMode::Variable);
    require(occupied.update(0.1),"Inventory energy audit failed for metabolism/leak");
    require(occupied.snapshot().atmospheres[0].inventoryEnergyKJ!=0,"Non-pathway inventory energy was not exposed");
    require(occupied.updateNode<AtmospherePathwaySettings>("door",[](auto& p){p.openFraction=1;}),"Occupied Door failed to open");
    for (int n=0;n<100;++n) require(occupied.update(0.01),"Wet blowdown energy/condensation audit failed");
    near(occupied.snapshot().atmospheres[0].residualKJ,0,1e-6);
    // A depleted gas node with several conductive contacts must not force the
    // Hull mesh to resolve meaningless nanokelvin differences at zero capacity.
    auto hullScene=s;hullScene.atmospherePathways[0].openFraction=1;
    auto neighbour=a;neighbour.id="neighbour";neighbour.startingTemperatureC=25;
    hullScene.atmospheres.push_back(neighbour);hullScene.hull=HullSettings{};
    ThermalLinkSettings partition;partition.id="partition";partition.aAtmosphereId="room";
    partition.bAtmosphereId="neighbour";partition.conductanceKWPerK=0.05;
    auto skin=partition;skin.id="skin";skin.bAtmosphereId="hull";skin.bKind=ThermalNodeKind::Hull;
    hullScene.thermalLinks={partition,skin};
    StationSystemsRuntime hullRuntime(hullScene,StationSystemsUpdateMode::Variable);
    for (int n=0;n<300;++n) if (!hullRuntime.update(0.1)) {
        const auto messages=hullRuntime.diagnostics();
        const auto air=hullRuntime.snapshot().atmospheres[0];
        throw std::runtime_error("Near-vacuum step "+std::to_string(n)+" capacity="+std::to_string(air.thermalCapacityKJPerK)
            +" pressure="+std::to_string(air.pressureKPa)+": "+(messages.empty() ? "no diagnostic" : messages.front().message));
    }
    require(hullRuntime.snapshot().healthy && hullRuntime.snapshot().atmospheres[0].pressureKPa<0.01,"Hull venting did not reach near vacuum");
    s.atmospherePathways[0].dischargeCoefficient=0.43;
    const auto yaml=serializeStationConfiguration(s);StationSystemsSettings loaded;
    require(parseStationConfiguration(yaml,loaded,&why),why.c_str());
    near(loaded.atmospherePathways[0].dischargeCoefficient,0.43);
    auto old=yaml;const auto at=old.find("dischargeCoefficient:");
    require(at!=std::string::npos,"Missing discharge field");
    const auto begin=old.rfind('\n',at)+1,endLine=old.find('\n',at);
    old.erase(begin,endLine==std::string::npos ? old.size()-begin : endLine-begin+1);
    const auto version=old.find("schemaVersion: 140");
    require(version!=std::string::npos,"Missing new pathway version");
    old.replace(version,18,"schemaVersion: 139");
    require(parseStationConfiguration(old,loaded,&why),why.c_str());
    near(loaded.atmospherePathways[0].dischargeCoefficient,0.7);
}
}
int main() { try {laws();conservation();runtime();} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
