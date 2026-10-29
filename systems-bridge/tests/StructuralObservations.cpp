#include <nsi/systems/CellAtmosphere.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <set>
#include <stdexcept>
using namespace nsi::systems;
using I=nsi::core::Installation;
namespace { bool tracking=false;std::size_t allocations=0; }
void* operator new(std::size_t n) {if(tracking) ++allocations;if(void* p=std::malloc(n?n:1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
namespace {
void require(bool b,const char* m) {if(!b) throw std::runtime_error(m);}
void near(long double a,long double b,long double tolerance=1e-6L) {require(std::abs(a-b)<tolerance,"Observation mismatch");}
void check() {
    I off(1,1);off.setHullType(std::nullopt);
    CellAtmosphereTransport optedOut(off,CellThermalMode::LocalStructure);
    require(optedOut.structuralSectorSummary(0).all.surfaces==0 && optedOut.structuralSurfaces({0,0,0},{}).total==0,"Opt-out published structure");
    CellAtmosphereTransport legacy(off,CellThermalMode::Geometry);
    require(!legacy.structure().enabled && legacy.structuralSummary({0,0,0}).all.surfaces==0,"Legacy advertised structural observations");
    I tall(1,1);tall.setHullType(I::HullType::BareSkin);tall.addLevel(1,1);tall.setSector(0,0,"Tall",2);
    CellAtmosphereTransport height(tall,CellThermalMode::LocalStructure);
    const auto tallSummary=height.structuralSummary({0,0,0});
    near(tallSummary.floor.areaM2,4);near(tallSummary.ceiling.areaM2,4);near(tallSummary.sides.areaM2,48);
    I offset(1,1);offset.addLevel(1,1,10,20);offset.setCell(1,0,0,I::SectorType::Interior);
    CellAtmosphereTransport shifted(offset,CellThermalMode::LocalStructure);
    require(shifted.structuralSummary({1,10,20}).all.surfaces>0 && shifted.structuralSummary({1,0,0}).all.surfaces==0,"Offset selected wrong Cell");
    I room(2,1);room.setHullType(I::HullType::BareSkin);room.setPartitionType(I::PartitionType::BarePartition);
    // A same-Sector physical partition gives both sides to one selection.
    // Global body energy and selected surface energy must still count each side once.
    CellAtmosphereTransport r(room,CellThermalMode::LocalStructure);
    const StructuralOwner cell{0,0,0};
    const auto summary=r.structuralSectorSummary(0);
    require(summary.floor.surfaces==2 && summary.ceiling.surfaces==2,"Floor/ceiling classification lost");
    near(summary.floor.areaM2,8);near(summary.ceiling.areaM2,8);
    near(*summary.all.areaWeightedTemperatureK(),293.15);
    auto manual=StructuralSurfaceSummary{};
    std::set<StructuralSurfaceId> ids;
    for(const auto& a:r.geometry().structure->assemblies) for(unsigned side=0;side<2;++side) if(a.owners[side]) {
        const auto& b=r.structure().bodies[a.bodies[side]];
        manual.capacityJPerK+=b.capacityJPerK();manual.storedEnergyJ+=b.energyJ();ids.insert(a.surfaceId(side));
    }
    require(ids.size()==summary.all.surfaces,"Sector surface duplication");
    near(summary.all.capacityJPerK,manual.capacityJPerK);near(summary.all.storedEnergyJ,manual.storedEnergyJ);
    std::array<StructuralSurfaceObservation,1> out;
    auto q=r.structuralSurfaces(cell,out);require(q.written==1 && q.total>1,"Bounded query did not truncate");
    const auto first=out[0].id;
    const auto observation=r.structuralSurface(first);require(observation && observation->gasTemperatureK && observation->cavity==0,"Gas/cavity observation missing");
    require(observation->gasContactWPerK>0 && observation->capacityJPerK>0,"Contact/capacity missing");
    near(observation->gasCapacityJPerK,r.fluid().primitives()[*observation->gasCell].capacityJPerK);
    q=r.structuralSurfaces(cell,out,1);require(out[0].id!=first && q.written==1,"Surface pagination ignored offset");
    require(r.structuralSurfaces(cell,out,SIZE_MAX).written==0,"Oversized offset wrapped");
    std::array<StructuralContactObservation,1> links;
    const auto graph=r.structuralContacts(first,links);require(graph.total>=1 && graph.written==1,"Local contact query missing");
    require(links[0].a==first || links[0].b==first,"Nonlocal graph edge returned");
    require(r.structuralSurface(links[0].a) && r.structuralSurface(links[0].b),"Edge endpoints unobservable");
    require(!r.structuralSummary({99,0,0}).all.areaWeightedTemperatureK() && !r.structuralSectorSummary(SIZE_MAX).all.surfaces,"Missing selection not unavailable");
    tracking=true;
    const auto local=r.structuralSummary(cell);r.structuralSurfaces(cell,out);r.structuralContacts(first,links);
    const bool ok=r.advance(.1);tracking=false;
    require(ok && allocations==0 && local.all.surfaces>0,"Observation/update allocated");
    const auto& p=r.structure().paths;
    near(p.gasIntoSolidJ,-r.sectors()[0].ledger.conductedHeatJ);
    near(r.structure().exteriorHeatJ,p.absorbedSolarJ-p.exteriorRadiationJ-p.portalEscapeJ);
    require(p.solidConductionMagnitudeJ>0 && p.interiorRadiationMagnitudeJ>=0 && p.exteriorRadiationJ>0,"Heat paths missing");
    near(r.structure().rates.exteriorRadiationW,p.exteriorRadiationJ/.1);
    require(r.structure().work.cavityClosureW<1e-8 && r.structure().modelRevision.size()>0 && r.structure().presetRevision.size()>0,"Revision/closure missing");
    near(r.structure().observedSeconds,.1);
    const auto committed=r.structure();const auto committedSurface=*r.structuralSurface(first);
    require(r.advance(0),"Pause refused");require(r.structure().paths==committed.paths && r.structure().rates==committed.rates,"Pause changed ledgers/rates");
    for(double seconds:{.1,1.}) {
        require(!r.advance(seconds,{},4096,1),"Failure fixture accepted");
        require(r.structure().paths==committed.paths && r.structure().rates==committed.rates,"Failure published trial heat paths");
        near(r.structure().observedSeconds,committed.observedSeconds);
        near(r.structuralSurface(first)->temperatureK,committedSurface.temperatureK);
        require(r.structure().work.paths==committed.work.paths,"Failure changed committed work paths");
    }
    CellAtmosphereTransport reset(room,CellThermalMode::LocalStructure);
    require(reset.structuralSurface(first).has_value() && reset.structure().paths==station::NativeThermalPaths{},"Reset identity/ledgers wrong");
    near(reset.structure().rates.exteriorRadiationW,0);near(reset.structure().observedSeconds,0);
    // Unresolved/absent gas cannot erase warm surfaces or masquerade as cold gas.
    auto geometry=expandCellAtmosphere(room,true);for(auto& c:geometry.mesh.cells) c.initial={};
    CellAtmosphereTransport vacuum(geometry,CellThermalMode::LocalStructure);
    require(vacuum.advance(.1),"Vacuum update failed");
    const auto v=vacuum.structuralSurface(first);
    require(v && !v->gasTemperatureK && v->gasContactWPerK==0 && v->temperatureK>200,"Vacuum structure/gas conflated");
    I absent(1,1);absent.setHullType(I::HullType::BareSkin);
    auto yaml=absent.serialize();const auto pos=yaml.find("Earth-like");require(pos!=std::string::npos,"Missing preset fixture");yaml.replace(pos,10,"Void");
    absent.deserialize(yaml);CellAtmosphereTransport missing(absent,CellThermalMode::LocalStructure);
    require(missing.structuralSectorSummary(0).all.surfaces>0 && missing.sectors().empty(),"Missing gas removed structure");
    missing.structuralSurfaces(cell,out);require(!out[0].gasCell && !out[0].gasTemperatureK,"Absent gas invented");
    auto gradient=expandCellAtmosphere(room,true);
    const auto patch=gradient.structure->assemblies.front().bodies[0];
    gradient.structure->bodies[patch]=station::ThermalBody({gradient.structure->bodies[patch].capacityJPerK(),200});
    CellAtmosphereTransport varied(gradient,CellThermalMode::LocalStructure);
    require(varied.advance(.1),"Gradient update refused");
    const auto range=varied.structuralSectorSummary(0).all;
    require(range.minTemperatureK<range.maxTemperatureK && varied.structure().paths.interiorRadiationMagnitudeJ>0,"Interior path/range hidden");
    I breach(1,1);breach.setHullType(I::HullType::BareSkin);breach.toggleWall(0,breach.level(0).verticalWall(0,0));
    CellAtmosphereTransport escaping(breach,CellThermalMode::LocalStructure);require(escaping.advance(.1),"Breach advance refused");
    require(escaping.structure().paths.portalEscapeJ>0,"Portal path not separated");
    std::array<StructuralPortalObservation,1> portals;
    require(escaping.structuralPortals(0,portals).written==1 && portals[0].outwardPowerW && *portals[0].outwardPowerW>0 && !portals[0].b,"Bounded portal observation missing");
    const auto field=portals[0].irradianceA;
    require(!escaping.advance(1.,{},4096,1),"Portal failure fixture accepted");
    escaping.structuralPortals(0,portals);require(portals[0].irradianceA==field,"Failure leaked trial irradiance");
    near(escaping.structure().exteriorHeatJ,escaping.structure().paths.absorbedSolarJ-escaping.structure().paths.exteriorRadiationJ-escaping.structure().paths.portalEscapeJ);
    // A second source slice fails after the first has already booked paths.
    CellAtmosphereTransport witness(room,CellThermalMode::LocalStructure);require(witness.advance(.1),"Budget witness failed");
    const auto budget=witness.structure().work.work;
    require(!r.advance(1.,{},4096,budget),"Late work-budget fixture accepted");
    require(r.structure().paths==committed.paths && r.structure().rates==committed.rates,"Late rollback leaked paths");
    I loop(2,2);loop.setHullType(I::HullType::BareSkin);loop.setPartitionType(I::PartitionType::BarePartition);
    loop.toggleWall(0,loop.level(0).verticalWall(1,0));
    CellAtmosphereTransport shared(loop,CellThermalMode::LocalStructure);
    std::set<StructuralSurfaceId> selected;std::size_t cursor=0,total=0;
    do {
        const auto page=shared.structuralSectorSurfaces(0,out,cursor++);total=page.total;
        if(page.written) require(selected.insert(out[0].id).second,"Same-Sector shared assembly duplicated");
    } while(cursor<total);
    require(selected.size()==shared.structuralSectorSummary(0).all.surfaces,"Summary/query dedup disagree");
    bool sameSector=false;
    for(const auto& a:shared.geometry().structure->assemblies) if(a.owners[0] && a.owners[1]) {
        sameSector=true;require(selected.contains(a.surfaceId(0)) && selected.contains(a.surfaceId(1)),"Shared sides omitted");
    }
    require(sameSector,"Missing same-Sector fixture");
    I doors(2,3);doors.setHullType(I::HullType::BareSkin);doors.setPartitionType(I::PartitionType::BarePartition);
    for(unsigned y=0;y<3;++y) doors.toggleWall(0,doors.level(0).verticalWall(1,y));
    const auto wall=doors.level(0).verticalWall(1,1);doors.setDoor(0,wall,true);
    CellAtmosphereTransport posed(doors,CellThermalMode::LocalStructure);
    StructuralSurfaceId leaf;
    for(const auto& a:posed.geometry().structure->assemblies) if(a.id.part==StructuralPart::DoorLeaf) leaf=a.surfaceId(1);
    const auto closed=*posed.structuralSurface(leaf);
    require(posed.advance(.1),"Door observation setup failed");
    posed.structuralPortals(0,portals);require(portals[0].irradianceA.has_value(),"Committed cavity field unavailable");
    const auto beforeControlEnergy=posed.structuralSurface(leaf)->storedEnergyJ;
    require(posed.setDoorOpen(0,wall,true),"Door control refused");
    posed.structuralPortals(0,portals);require(portals[0].areaM2==1.8 && !portals[0].irradianceA && !portals[0].outwardPowerW,"Door left stale field available");
    require(!posed.advance(1.,{},4096,1),"Door rollback fixture accepted");
    const auto opened=*posed.structuralSurface(leaf);
    require(opened.id==closed.id && opened.cavity!=closed.cavity && opened.owner!=closed.owner,"Parked leaf ownership not observed");
    near(opened.storedEnergyJ,beforeControlEnergy);
    const auto q0=posed.structuralSectorSummary(0),q1=posed.structuralSectorSummary(1);
    require(posed.setDoorOpen(0,wall,false),"Door close refused");
    require(posed.structuralSurface(leaf)->owner==closed.owner,"Closing failed to restore observed side");
    near(q0.all.storedEnergyJ+q1.all.storedEnergyJ,posed.structuralSectorSummary(0).all.storedEnergyJ+posed.structuralSectorSummary(1).all.storedEnergyJ);
    // Shared partition is one assembly with two distinct side IDs; capacity is
    // split, never assigned in full to each Sector selection.
    room.toggleWall(0,room.level(0).verticalWall(1,0));
    CellAtmosphereTransport divided(room,CellThermalMode::LocalStructure);
    for(const auto& a:divided.geometry().structure->assemblies) if(a.owners[0] && a.owners[1]) {
        const auto a0=divided.structuralSurface(a.surfaceId(0)),a1=divided.structuralSurface(a.surfaceId(1));
        require(a0->id!=a1->id && a0->cavity!=a1->cavity,"Shared assembly lost side identity");
        near(a0->capacityJPerK+a1->capacityJPerK,a.areaM2*a.material.arealCapacityJPerM2K);
    }
}
void benchmark() {
    using Clock=std::chrono::steady_clock;
    auto ms=[](auto start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();};
    I room(64,64);room.setHullType(I::HullType::BareSkin);
    auto start=Clock::now();CellAtmosphereTransport r(room,CellThermalMode::LocalStructure);const auto build=ms(start);
    double update=0,local=0,sector=0;long double sink=0;
    std::array<StructuralSurfaceObservation,16> surfaces;
    for(int i=-2;i<10;++i) {
        start=Clock::now();require(r.advance(.1,{},4096,100000),"Benchmark update refused");const auto u=ms(start);
        start=Clock::now();for(int j=0;j<1000;++j) {r.structuralSurfaces({0,32,32},surfaces);sink+=r.structuralSummary({0,32,32}).all.storedEnergyJ;}const auto l=ms(start)/1000;
        start=Clock::now();sink+=r.structuralSectorSummary(0).all.storedEnergyJ;const auto s=ms(start);
        if(i>=0) {update+=u;local+=l;sector+=s;}
    }
    const auto& s=r.structure();
    std::cout<<"model,preset,cells,bodies,assemblies,contacts,build_ms,update_with_commit_ms,local_query_ms,sector_summary_ms,sink\n"
        <<s.modelRevision<<','<<s.presetRevision<<",4096,"<<s.bodies.size()<<','<<s.assemblies<<','<<s.solidContacts<<','<<build<<','<<update/10<<','<<local/10<<','<<sector/10<<','<<sink<<'\n';
}
}
int main(int argc,char**) {try {if(argc>1) benchmark();else check();} catch(const std::exception& e) {tracking=false;std::cerr<<e.what()<<'\n';return 1;}}
