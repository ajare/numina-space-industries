#include <nsi/systems/CellAtmosphere.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
using namespace nsi::systems;
using I=nsi::core::Installation;
namespace {bool trackAllocations=false;std::size_t allocations=0;}
void* operator new(std::size_t n) {
    if (trackAllocations) ++allocations;
    if (void* p=std::malloc(n ? n : 1)) return p;throw std::bad_alloc();
}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
namespace {
void require(bool x,const char* message) {if (!x) throw std::runtime_error(message);}
void near(double a,double b,double tolerance=1e-9) {
    require(std::abs(a-b)<=tolerance*std::max({1.0,std::abs(a),std::abs(b)}),"Cell atmosphere value/conservation mismatch");
}
void heatBudgets(const I& installation,const CellAtmosphereExpansion& e) {
    std::vector<double> sum(e.thermal.links.size(),0),areas(sum.size(),0);
    for (const auto& c:e.thermalContacts) {
        require(c.a<e.mesh.cells.size() && (c.b==SIZE_MAX || c.b<e.mesh.cells.size()),"Invalid thermal endpoint");
        require(c.areaM2>0 && c.conductanceKWPerK>0,"Invalid thermal contact");sum[c.link]+=c.conductanceKWPerK;areas[c.link]+=c.areaM2;
    }
    for (std::size_t i=0;i<sum.size();++i) {
        const auto& link=e.thermal.links[i];near(sum[i],link.conductanceKWPerK,1e-13);
        const double u=link.bKind==station::ThermalNodeKind::Hull ? I::hullSpec(*installation.hullType()).transmittanceWPerM2K
            : I::partitionSpec(*installation.partitionType()).transmittanceWPerM2K;
        near(u*areas[i]/1000,link.conductanceKWPerK,1e-13);
    }
}
void closes(const CellSectorAtmosphere& initial,const CellSectorAtmosphere& end) {
    const auto& l=end.ledger;
    for (int g=0;g<4;++g) near(initial.massKg[g]+l.sourceKg[g]+l.transportedKg[g],end.massKg[g]);
    near(initial.totalEnergyJ+l.inventoryEnergyJ+l.transportedEnergyJ+l.externalHeatJ+l.occupantHeatJ+l.latentHeatJ,end.totalEnergyJ);
}
void momentumCloses(const CellAtmosphereTransport& gas) {
    const auto& m=gas.momentum();
    for (int axis=0;axis<2;++axis) near(m.starting[axis]+m.sources[axis]+m.wallImpulse[axis]-m.exteriorLost[axis],m.current[axis],1e-8);
}
void geometry() {
    I scene(4,3);const auto authored=scene.serialize();auto e=expandCellAtmosphere(scene);
    require(e.mesh.cells.size()==12 && e.mesh.faces.size()==31,"Incorrect Cell topology");heatBudgets(scene,e);
    double volume=0;for (const auto& c:e.mesh.cells) {near(c.volumeM3,12);volume+=c.volumeM3;}
    near(volume,e.atmospheres[0].volumeM3);
    CellAtmosphereTransport closed(e);const auto initial=closed.sectors()[0];
    require(closed.advance(0.1),"Sealed scene failed");near(closed.sectors()[0].meanPressurePa,101325);
    for (int g=0;g<4;++g) near(initial.massKg[g],closed.sectors()[0].massKg[g]);
    scene.toggleWall(0,scene.level(0).horizontalWall(1,0));e=expandCellAtmosphere(scene);heatBudgets(scene,e);
    require(std::count_if(e.mesh.faces.begin(),e.mesh.faces.end(),[](const auto& f){return f.b==SIZE_MAX && f.openFraction==1;})==1,"Absent exterior Wall did not breach");
    CellAtmosphereTransport breach(e);const auto before=breach.sectors()[0];require(breach.advance(0.1),"Full-face breach failed");
    const auto& after=breach.sectors()[0];require(after.massKg[1]<before.massKg[1] && after.ledger.exteriorLostKg[1]>0,"Breach did not discharge");
    require(after.minPressurePa<after.maxPressurePa,"Sector stayed well mixed");closes(before,after);momentumCloses(breach);
    require(scene.serialize()!=authored,"Fixture did not remove Wall");const auto changed=scene.serialize();
    require(breach.advance(0.1) && scene.serialize()==changed,"Simulation mutated geometry");
    I hole(5,3);hole.setCell(0,2,1,I::SectorType::Void);
    hole.toggleWall(0,hole.level(0).verticalWall(2,1));e=expandCellAtmosphere(hole);heatBudgets(hole,e);
    require(e.cellIndices[0][7]==SIZE_MAX,"Geometric Void acquired gas");
    require(std::any_of(e.mesh.faces.begin(),e.mesh.faces.end(),[](const auto& f){return f.b==SIZE_MAX && f.openFraction==1;}),"Internal Void did not breach");
    I tall(std::vector<I::Level>{I::Level(2,1),I::Level(2,1,I::SectorType::Void),I::Level(2,1)});
    tall.setSector(0,0,"Tall",2);tall.setOffset(2,-1,0);e=expandCellAtmosphere(tall);heatBudgets(tall,e);
    require(e.mesh.cells.size()==4 && e.atmospheres.size()==2,"Height introduced vertical fluid layers");
    near(e.mesh.cells[0].volumeM3,24);near(e.mesh.cells[2].volumeM3,12);
    for (const auto& f:e.mesh.faces) {
        near(f.areaM2,6);require(f.b==SIZE_MAX || e.mesh.cells[f.a].level==e.mesh.cells[f.b].level,"Vertical fluid face generated");
    }
    unsigned floors=0;
    for (const auto& c:e.thermalContacts) if (c.b!=SIZE_MAX && e.mesh.cells[c.a].level!=e.mesh.cells[c.b].level) {
        ++floors;near(c.areaM2,4);
        require(e.mesh.cells[c.a].gridIndex==0 && e.mesh.cells[c.b].gridIndex==1,"Offset floor contact mislocated");
    }
    require(floors==1,"Tall/offset floor overlap counted incorrectly");
}
void doors() {
    I scene(5,5);scene.setHullType(std::nullopt);const auto door=scene.level(0).horizontalWall(2,0);scene.setDoor(0,door,true);
    auto e=expandCellAtmosphere(scene);require(e.doors.size()==1 && e.doors[0].face!=SIZE_MAX,"Door lacks Cell face");
    const auto face=e.doors[0].face;near(e.doors[0].fullOpenFraction,0.3);
    CellAtmosphereTransport gas(e);const auto before=gas.sectors()[0];
    require(gas.advance(0.1),"Closed Door failed");near(gas.sectors()[0].massKg[1],before.massKg[1]);
    require(!gas.setDoorOpen(0,0,true) && !gas.setDoorOpen(1,door,true),"Invalid Door accepted");
    require(gas.setDoorOpen(0,door,true),"Door control failed");
    const auto& aperture=gas.fluid().mesh().faces[face];near(aperture.areaM2*aperture.openFraction,1.8);near(aperture.dischargeCoefficient,0.7);
    require(gas.advance(0.1) && gas.sectors()[0].massKg[1]<before.massKg[1],"Open Door did not discharge");
    const double nitrogen=gas.sectors()[0].massKg[1];gas.setDoorOpen(0,door,false);
    require(gas.advance(0.1),"Reclosing Door failed");near(gas.sectors()[0].massKg[1],nitrogen);closes(before,gas.sectors()[0]);momentumCloses(gas);
    I pair(5,5);const auto grid=pair.level(0);const auto gap=grid.horizontalWall(2,2);
    for (unsigned x=0;x<5;++x) pair.toggleWall(0,grid.horizontalWall(x,2));pair.setDoor(0,gap,true);
    auto yaml=pair.serialize();const auto preset=yaml.find("atmosphere: Earth-like");
    yaml.replace(preset,std::string("atmosphere: Earth-like").size(),"atmosphere: Void");pair.deserialize(yaml);
    e=expandCellAtmosphere(pair);require(e.atmospheres.size()==1 && e.doors[0].face==SIZE_MAX,"Void preset became vacuum endpoint");
    CellAtmosphereTransport absent(e);const auto sealed=absent.sectors()[0];
    require(absent.setDoorOpen(0,gap,true) && absent.advance(0.1),"Door adjoining absent compartment failed");
    for (int g=0;g<4;++g) near(absent.sectors()[0].massKg[g],sealed.massKg[g]);
    I loop(5,5);const auto lg=loop.level(0);for (unsigned x:{1,3}) loop.toggleWall(0,lg.horizontalWall(x,2));loop.setDoor(0,gap,true);
    e=expandCellAtmosphere(loop);require(e.doors[0].face!=SIZE_MAX,"Same-Sector Door discarded");
    const auto& f=e.mesh.faces[e.doors[0].face];require(f.b!=SIZE_MAX && e.mesh.cells[f.a].atmosphere==e.mesh.cells[f.b].atmosphere,"Loop Door mapped to incorrect compartments");
}
void loadsAndPhase() {
    I scene(2,1);scene.setHullType(std::nullopt);auto e=expandCellAtmosphere(scene);auto& load=e.atmospheres[0];
    load.occupants=2;load.oxygenUsePerOccupantKgPerS=0.01;load.co2ProductionPerOccupantKgPerS=0.005;
    load.moistureAdditionPerOccupantKgPerS=0.001;load.sensibleHeatPerOccupantKW=0.05;load.leakRateKgPerS=0.02;
    CellAtmosphereTransport gas(e);const auto initial=gas.sectors()[0];
    trackAllocations=true;const bool advanced=gas.advance(0.1);trackAllocations=false;
    require(advanced,"Cell loads failed");require(allocations==0,"Native source/phase update allocated heap memory");
    const auto& end=gas.sectors()[0];closes(initial,end);near(end.ledger.oxygenConsumedKg,0.002);near(end.ledger.co2ProducedKg,0.001);
    near(end.ledger.vapourAddedKg,0.0002);near(end.ledger.occupantHeatJ,10);momentumCloses(gas);
    double leaked=0;for (const double kg:end.ledger.leakedKg) leaked+=kg;near(leaked,0.002);
    near(gas.fluid().primitives()[0].temperatureK,gas.fluid().primitives()[1].temperatureK);
    CellAtmosphereTransport fast(e),small(e);require(fast.advance(1),"Accelerated source update failed");
    std::vector<station::CellFluidState> totalFaces(fast.faceTransfers().size());
    for (int i=0;i<10;++i) {
        require(small.advance(0.1),"Reference source update failed");
        for (std::size_t f=0;f<totalFaces.size();++f) for (int q=0;q<7;++q) totalFaces[f].q[q]+=small.faceTransfers()[f].q[q];
    }
    for (std::size_t f=0;f<totalFaces.size();++f) for (int q=0;q<7;++q) near(fast.faceTransfers()[f].q[q],totalFaces[f].q[q]);
    for (std::size_t i=0;i<fast.fluid().states().size();++i) for (int q=0;q<7;++q) near(fast.fluid().states()[i].q[q],small.fluid().states()[i].q[q]);
    closes(initial,fast.sectors()[0]);
    auto local=e;local.atmospheres[0].leakRateKgPerS=0;
    const auto localGas=local.mesh.cells[0].initial.q;
    local.mesh.cells[0].initial=station::makeCellFluidState({0,localGas[1],localGas[2],localGas[3]},293.15);
    CellAtmosphereTransport uneven(local);require(uneven.advance(0.1),"Same-Sector local metabolism failed");
    near(uneven.sectors()[0].ledger.oxygenConsumedKg,0.0015);near(uneven.sectors()[0].ledger.occupantHeatJ,7.5);
    momentumCloses(uneven);
    scene.toggleWall(0,scene.level(0).verticalWall(1,0));e=expandCellAtmosphere(scene);
    for (auto& a:e.atmospheres) {a.occupants=1;a.oxygenUsePerOccupantKgPerS=0.01;a.co2ProductionPerOccupantKgPerS=0.005;a.sensibleHeatPerOccupantKW=0.05;a.moistureAdditionPerOccupantKgPerS=0.001;}
    auto masses=e.mesh.cells[0].initial.q;masses[0]=0;
    e.mesh.cells[0].initial=station::makeCellFluidState({masses[0],masses[1],masses[2],masses[3]},293.15);
    CellAtmosphereTransport starved(e);require(starved.advance(0.1),"Local starvation failed");
    const auto left=e.mesh.cells[0].atmosphere,right=e.mesh.cells[1].atmosphere;
    near(starved.sectors()[left].ledger.oxygenConsumedKg,0);near(starved.sectors()[left].ledger.co2ProducedKg,0);near(starved.sectors()[left].ledger.occupantHeatJ,0);
    require(starved.sectors()[left].ledger.vapourAddedKg>0 && starved.sectors()[right].ledger.occupantHeatJ>0,"Local starvation contaminated other Cells");
    e=expandCellAtmosphere(scene);const auto original=e.mesh.cells[0].initial.q;
    e.mesh.cells[0].initial=station::makeCellFluidState({original[0],original[1],original[2],original[3]},273.15);
    CellAtmosphereTransport condensing(e);const auto cold=condensing.sectors()[left],warm=condensing.sectors()[right];
    trackAllocations=true;const bool condensed=condensing.advance(0.1);trackAllocations=false;
    require(condensed,"Local condensation failed");require(allocations==0,"Condensation update allocated heap memory");
    require(condensing.sectors()[left].ledger.condensedKg>0 && condensing.sectors()[right].ledger.condensedKg==0,"Condensation was not local");
    closes(cold,condensing.sectors()[left]);closes(warm,condensing.sectors()[right]);
    const auto& p=condensing.fluid().primitives()[0];const double tc=p.temperatureK-station::StationCelsiusOffset;
    const auto pv=station::stationAtmospherePartialPressureKPa(condensing.fluid().states()[0].q[3],station::StationMolarMassWaterKgPerMol,12,tc);
    require(pv<=station::stationSaturationVapourPressureKPa(tc)+1e-10,"Published supersaturated Cell");
    std::vector<double> heat(condensing.fluid().states().size(),0);heat[1]=100;
    require(condensing.advance(0.1,heat),"Localized external heat failed");near(condensing.sectors()[right].ledger.externalHeatJ,100);
    closes(warm,condensing.sectors()[right]);
}
void rollback() {
    I scene(3,2);scene.setHullType(std::nullopt);scene.toggleWall(0,scene.level(0).horizontalWall(1,0));
    auto e=expandCellAtmosphere(scene);e.atmospheres[0].occupants=1;e.atmospheres[0].oxygenUsePerOccupantKgPerS=0.001;
    e.atmospheres[0].moistureAdditionPerOccupantKgPerS=0.01;CellAtmosphereTransport gas(e);
    auto before=std::vector<station::CellFluidState>(gas.fluid().states().begin(),gas.fluid().states().end());
    const auto initial=gas.sectors()[0];require(!gas.advance(0.1,{},0),"Work limit ignored");
    require(!gas.diagnostic().empty() && gas.elapsedSeconds()==0 && gas.sectors()[0].ledger==initial.ledger,"Failed update committed sources/time");
    for (std::size_t i=0;i<before.size();++i) require(before[i].q==gas.fluid().states()[i].q,"Failed update changed gas");
    require(gas.advance(0.1),"Retry failed");closes(initial,gas.sectors()[0]);const auto committed=gas.sectors()[0];const auto committedMomentum=gas.momentum();
    before.assign(gas.fluid().states().begin(),gas.fluid().states().end());
    auto reference=gas;require(reference.advance(0.1),"Work reference failed");const auto budget=reference.work().substeps;
    require(budget>0 && !gas.advance(0.2,{},budget),"Acoustic work budget reset between source slices");
    require(gas.sectors()[0].ledger==committed.ledger,"Multi-slice failure committed sources");
    std::vector<double> invalid(before.size(),-1e100);
    require(!gas.advance(0.1,invalid),"Invalid heat accepted");near(gas.elapsedSeconds(),0.1);
    require(gas.sectors()[0].ledger==committed.ledger && gas.momentum()==committedMomentum,"Late failure committed ledgers");
    for (std::size_t i=0;i<before.size();++i) require(before[i].q==gas.fluid().states()[i].q,"Late failure changed gas");
}
}
int main() {try {geometry();doors();loadsAndPhase();rollback();std::cout<<"Cell atmosphere bridge passed\n";return 0;}
catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
