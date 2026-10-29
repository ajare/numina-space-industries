#include <nsi/systems/CellAtmosphere.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
using namespace nsi::systems;
using I=nsi::core::Installation;
namespace {bool tracking=false;std::size_t allocations=0;}
void* operator new(std::size_t n) {if (tracking) ++allocations;if (void* p=std::malloc(n ? n : 1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
namespace {
void require(bool x,const char* why) {if (!x) throw std::runtime_error(why);}
void near(long double a,long double b,long double tolerance=1e-9L) {require(std::abs(a-b)<=tolerance*std::max({1.0L,std::abs(a),std::abs(b)}),"Spatial thermal value/energy mismatch");}
void closeGas(const CellSectorAtmosphere& start,const CellSectorAtmosphere& end) {
    const auto& l=end.ledger;
    near(start.totalEnergyJ+l.inventoryEnergyJ+l.transportedEnergyJ+l.externalHeatJ+l.occupantHeatJ+l.latentHeatJ+l.conductedHeatJ,end.totalEnergyJ);
    for (int g=0;g<4;++g) near(start.massKg[g]+l.sourceKg[g]+l.transportedKg[g],end.massKg[g]);
}
void setTemperature(CellAtmosphereExpansion& e,std::size_t i,double t,double scale=1) {
    const auto q=e.mesh.cells[i].initial.q;
    e.mesh.cells[i].initial=station::makeCellFluidState({q[0]*scale,q[1]*scale,q[2]*scale,q[3]*scale},t);
}
void check() {
    I single(1,1);single.setHullType(I::HullType::BareSkin);
    CellAtmosphereTransport gas(single,CellThermalMode::Geometry);const auto start=gas.sectors()[0];const auto hullStart=*gas.thermal().hull;
    tracking=true;const bool advanced=gas.advance(0.1);tracking=false;
    require(advanced,"Spatial Hull step failed");require(allocations==0,"Geometry thermal step allocated heap memory");
    const auto& end=gas.sectors()[0];const auto& hull=*gas.thermal().hull;
    require(end.temperatureK<start.temperatureK && end.ledger.conductedHeatJ<0,"Hull cooling did not reach gas");
    require(hull.emittedJ>0 && hull.absorbedJ==0,"Night-side radiation incorrect");
    near(end.ledger.conductedHeatJ+hull.conductedJ,0);
    near(hullStart.energyJ()+hull.absorbedJ-hull.emittedJ+hull.conductedJ,hull.energyJ());closeGas(start,end);
    near(gas.thermal().linkEnergyJ[0],-end.ledger.conductedHeatJ,1e-7);
    auto solarExpansion=expandCellAtmosphere(single);
    solarExpansion.thermal.solarCycle=station::ProfileSettings{1,1,{{0,1},{0.5,0}}};
    CellAtmosphereTransport solar(solarExpansion,CellThermalMode::Geometry);
    tracking=true;const bool illuminated=solar.advance(0.1);tracking=false;
    require(illuminated && allocations==0,"Cyclic solar profile allocated during thermal stepping");
    require(solar.thermal().hull->absorbedJ>0,"Stellar input did not enter Hull ledger");
    require(solar.advance(0.4),"Solar progression failed");const auto absorbed=solar.thermal().hull->absorbedJ;
    require(solar.advance(0.1),"Night progression failed");near(solar.thermal().hull->absorbedJ,absorbed);
    CellAtmosphereTransport longRun(single,CellThermalMode::Geometry);
    const auto initialGas=longRun.sectors()[0];const auto initialHull=*longRun.thermal().hull;
    require(longRun.advance(200),"Long coupled cooling failed");
    closeGas(initialGas,longRun.sectors()[0]);
    const auto& cooledHull=*longRun.thermal().hull;
    near(initialHull.energyJ()+cooledHull.absorbedJ-cooledHull.emittedJ+cooledHull.conductedJ,cooledHull.energyJ());
    require(longRun.sectors()[0].ledger.condensedKg>0,"Long cooling did not couple condensation");
    I field(5,5);CellAtmosphereTransport local(field,CellThermalMode::Geometry);
    require(local.advance(0.1),"Localized skin solve failed");
    require(local.fluid().primitives()[0].temperatureK<local.fluid().primitives()[12].temperatureK,"Skin cooling not localized by Cell area");
    I pair(2,1);pair.setPartitionType(I::PartitionType::BarePartition);pair.toggleWall(0,pair.level(0).verticalWall(1,0));
    auto e=expandCellAtmosphere(pair);setTemperature(e,0,313.15);
    CellAtmosphereTransport partition(e,CellThermalMode::Geometry);const auto a=partition.sectors()[0],b=partition.sectors()[1];
    require(partition.advance(0.1),"Partition solve failed");
    require(partition.fluid().primitives()[0].temperatureK<313.15 && partition.fluid().primitives()[1].temperatureK>293.15,"Partition heat went to wrong Cells");
    closeGas(a,partition.sectors()[0]);closeGas(b,partition.sectors()[1]);
    for (int g=0;g<4;++g) {near(a.massKg[g],partition.sectors()[0].massKg[g]);near(b.massKg[g],partition.sectors()[1].massKg[g]);}
    I levels(std::vector<I::Level>{I::Level(1,1),I::Level(1,1)});levels.setPartitionType(I::PartitionType::BarePartition);
    e=expandCellAtmosphere(levels);setTemperature(e,0,313.15);CellAtmosphereTransport floor(e,CellThermalMode::Geometry);
    const auto lower=floor.sectors()[0],upper=floor.sectors()[1];require(floor.advance(0.1),"Cross-Level floor heat failed");
    require(floor.fluid().primitives()[1].temperatureK>293.15,"Floor heat missing");
    for (int g=0;g<4;++g) {near(lower.massKg[g],floor.sectors()[0].massKg[g]);near(upper.massKg[g],floor.sectors()[1].massKg[g]);}
    closeGas(lower,floor.sectors()[0]);closeGas(upper,floor.sectors()[1]);
    // A trace gas bridges multiple finite bodies without explicit tiny steps.
    I three(3,1);three.setPartitionType(I::PartitionType::BarePartition);
    three.toggleWall(0,three.level(0).verticalWall(1,0));three.toggleWall(0,three.level(0).verticalWall(2,0));
    e=expandCellAtmosphere(three);setTemperature(e,0,313.15);setTemperature(e,1,293.15,1e-19);
    CellAtmosphereTransport trace(e,CellThermalMode::Geometry);const auto tiny=trace.sectors()[1];
    require(trace.advance(0.1),"Stiff trace-gas heat solve failed");
    const auto& p=trace.fluid().primitives()[1];require(p.temperatureK>290 && p.temperatureK<314,"Trace gas temperature became nonphysical");
    require(p.capacityJPerK<1e-8 && trace.thermalIterations()<20,"Trace node gained hidden capacity or excessive work");
    closeGas(tiny,trace.sectors()[1]);
    auto unresolved=e;const auto material=expandCellAtmosphere(three).mesh.cells[1].initial.q;
    unresolved.mesh.cells[1].initial=station::makeCellFluidState({material[0]*1e-20,material[1]*1e-20,material[2]*1e-20,material[3]*1e-20},293.15);
    CellAtmosphereTransport retained(unresolved,CellThermalMode::Geometry);const auto retainedTrace=retained.fluid().states()[1];
    require(retained.advance(0.1),"Unresolved trace step failed");
    require(retained.fluid().states()[1].q==retainedTrace.q,"Unresolved trace energy/material was discarded");
    e.mesh.cells[1].initial={};CellAtmosphereTransport empty(e,CellThermalMode::Geometry);
    require(empty.advance(0.1),"Empty gas heat step failed");near(empty.sectors()[1].totalEnergyJ,0);near(empty.sectors()[1].ledger.conductedHeatJ,0);
    // A failed solve after radiation/source changes rolls back every body.
    e=expandCellAtmosphere(single);e.atmospheres[0].occupants=1;e.atmospheres[0].oxygenUsePerOccupantKgPerS=0.001;
    CellAtmosphereTransport transactional(e,CellThermalMode::Geometry);
    const auto before=transactional.fluid().states()[0];const auto h=*transactional.thermal().hull;const auto l=transactional.sectors()[0].ledger;
    require(!transactional.advance(0.1,{},4096,0),"Thermal work limit ignored");
    require(!transactional.diagnostic().empty() && transactional.elapsedSeconds()==0,"Thermal refusal committed time");
    require(transactional.fluid().states()[0].q==before.q && *transactional.thermal().hull==h && transactional.sectors()[0].ledger==l,"Thermal refusal did not roll back gas/Hull/sources");
    require(!transactional.advance(0.2,{},4096,1),"Thermal work budget reset between source slices");
    require(transactional.fluid().states()[0].q==before.q && *transactional.thermal().hull==h && transactional.sectors()[0].ledger==l,"Multi-slice thermal failure committed partial state");
    require(transactional.advance(0.1),"Thermal retry failed");const auto committed=transactional.fluid().states()[0];
    const auto committedHull=*transactional.thermal().hull;const auto links=transactional.thermal().linkEnergyJ;
    const std::vector<double> impossible{-1e100};require(!transactional.advance(0.1,impossible),"Impossible heat was accepted");
    require(transactional.fluid().states()[0].q==committed.q && *transactional.thermal().hull==committedHull && transactional.thermal().linkEnergyJ==links,"Late failure committed thermal energy");
    e.atmospheres[0].reservoirId="missing";bool refused=false;
    try {CellAtmosphereTransport invalid(e,CellThermalMode::Geometry);} catch (const std::invalid_argument&) {refused=true;}
    require(refused,"Unresolved reservoir silently ignored");
}
}
int main() {try {check();std::cout<<"Spatial thermal coupling passed\n";return 0;}
catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
