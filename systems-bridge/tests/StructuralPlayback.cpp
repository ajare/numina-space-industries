#include <nsi/systems/CellAtmosphere.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
using namespace nsi::systems;
using I=nsi::core::Installation;
namespace { bool tracking=false;std::size_t allocations=0; }
void* operator new(std::size_t n) {if (tracking) ++allocations;if (void* p=std::malloc(n ? n : 1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
namespace {
void require(bool x,const char* why) {if (!x) throw std::runtime_error(why);}
void advance(CellAtmosphereTransport& runtime,double seconds) {
    if (!runtime.advance(seconds)) throw std::runtime_error(runtime.diagnostic());
}
long double solidEnergy(const CellAtmosphereTransport& runtime) {
    long double total=0;for (const auto& b:runtime.structure().bodies) total+=b.energyJ();return total;
}
void audit(const CellAtmosphereTransport& runtime) {
    require(std::abs(runtime.structure().residualJ)<1e-6L,"Gas/solid/source/advection/radiation audit failed");
}
void sameBodies(const std::vector<station::ThermalBody>& a,const std::vector<station::ThermalBody>& b) {
    require(a.size()==b.size(),"Rollback changed body count");
    for (std::size_t i=0;i<a.size();++i) require(a[i].energyJ()==b[i].energyJ(),"Rollback changed body energy");
}
void check() {
    I room(1,1);room.setHullType(I::HullType::BareSkin);
    CellAtmosphereTransport runtime(room,CellThermalMode::LocalStructure);
    require(!runtime.structure().bodies.empty() && !runtime.thermal().hull && runtime.thermal().linkEnergyJ.empty(),"Legacy and structural backends overlap");
    tracking=true;const bool ok=runtime.advance(0.1);tracking=false;
    require(ok && allocations==0,"Local-solid successful update failed/allocated");audit(runtime);
    require(runtime.structure().exteriorHeatJ<0,"Exterior solid radiation missing");
    const auto solidStart=runtime.structure().startingEnergyJ-runtime.geometry().mesh.cells[0].initial.q[6];
    const auto gasHeat=runtime.sectors()[0].ledger.conductedHeatJ;
    require(std::abs(solidEnergy(runtime)-solidStart-runtime.structure().exteriorHeatJ+gasHeat)<1e-7L,"Film did not book equal/opposite solid energy");
    auto empty=expandCellAtmosphere(room,true);empty.mesh.cells[0].initial={};
    CellAtmosphereTransport vacuum(empty,CellThermalMode::LocalStructure);
    const auto initialSolid=solidEnergy(vacuum);advance(vacuum,1);
    require(!vacuum.fluid().primitives()[0].resolvedGas && vacuum.fluid().states()[0].q==station::CellFluidState{}.q,"Evacuation manufactured gas");
    require(solidEnergy(vacuum)<initialSolid && vacuum.structure().bodies.size()==empty.structure->bodies.size(),"Evacuated solids stopped evolving");audit(vacuum);
    auto leaking=expandCellAtmosphere(room,true);leaking.atmospheres[0].leakRateKgPerS=1e9;
    CellAtmosphereTransport evacuated(leaking,CellThermalMode::LocalStructure);
    advance(evacuated,0.1);require(!evacuated.fluid().primitives()[0].resolvedGas,"Leak fixture did not evacuate");
    const auto afterEvacuation=solidEnergy(evacuated);advance(evacuated,0.1);
    require(solidEnergy(evacuated)<afterEvacuation,"Structure stopped after dynamic evacuation");audit(evacuated);
    // Tall/offset owners must map to their source fluid Cell, not local indices
    // or the Level containing an upper patch of the same height claim.
    I tall(2,1);tall.addLevel(1,1);tall.addLevel(2,1,1,0);
    tall.setSector(0,0,"Tall",2);tall.setCell(2,0,0,I::SectorType::Interior);tall.setCell(2,1,0,I::SectorType::Interior);
    auto tallGeometry=expandCellAtmosphere(tall,true);
    for (auto& body:tallGeometry.structure->bodies) body=station::ThermalBody({body.capacityJPerK(),250});
    CellAtmosphereTransport tallRuntime(tallGeometry,CellThermalMode::LocalStructure);advance(tallRuntime,0.1);audit(tallRuntime);
    for (const auto& sector:tallRuntime.sectors()) require(sector.ledger.conductedHeatJ<0,"Tall/offset gas contact lost owner");
    require(tallGeometry.structure->cavityCount==tallRuntime.sectors().size(),"Tall claims created extra cavity fields");
    // Isolate radiation from films/conduction/exterior faces, then prove a
    // closed enclosure redistributes heat without manufacturing a space sink.
    auto radiationOnly=[](const I& installation) {
        auto g=expandCellAtmosphere(installation,true);
        for (auto& cell:g.mesh.cells) cell.initial={};
        g.structure->contacts.clear();
        for (auto& assembly:g.structure->assemblies) {
            if (!assembly.owners[0] || !assembly.owners[1]) assembly.material.secondSide.emissivity=0;
        }
        return g;
    };
    auto sealed=radiationOnly(room);
    std::size_t coldSurface=0;
    for (const auto& a:sealed.structure->assemblies) for (unsigned side=0;side<2;++side)
        if (a.owners[side]) coldSurface=a.bodies[side];
    auto& coldBody=sealed.structure->bodies[coldSurface];coldBody=station::ThermalBody({coldBody.capacityJPerK(),200});
    CellAtmosphereTransport sealedRoom(sealed,CellThermalMode::LocalStructure);
    const auto sealedStart=solidEnergy(sealedRoom);
    advance(sealedRoom,.1);audit(sealedRoom);
    require(sealedRoom.structure().bodies[coldSurface].temperatureK()>200,"Closed cavity failed to warm cold surface");
    require(std::abs(solidEnergy(sealedRoom)-sealedStart)<1e-7 && std::abs(sealedRoom.structure().exteriorHeatJ)<1e-12,"Closed cavity invented exterior sink");
    I opaque(2,1);opaque.setHullType(I::HullType::BareSkin);opaque.setPartitionType(std::nullopt);
    opaque.toggleWall(0,opaque.level(0).verticalWall(1,0));
    auto isolated=radiationOnly(opaque);
    require(isolated.structure->cavityCount==2,"Opaque unmodelled partition lost cavity boundary");
    for (const auto& a:isolated.structure->assemblies) for (unsigned side=0;side<2;++side) if (a.owners[side]) {
        auto& b=isolated.structure->bodies[a.bodies[side]];
        b=station::ThermalBody({b.capacityJPerK(),a.owners[side]->worldX==0?400.:200.});
    }
    CellAtmosphereTransport opaqueRooms(isolated,CellThermalMode::LocalStructure);
    const auto isolatedBodies=opaqueRooms.structure().bodies;
    advance(opaqueRooms,.1);audit(opaqueRooms);
    for (std::size_t i=0;i<isolatedBodies.size();++i)
        require(std::abs(opaqueRooms.structure().bodies[i].energyJ()-isolatedBodies[i].energyJ())<1e-9,"Reflecting opaque partition became portal");
    I breached=room;breached.toggleWall(0,breached.level(0).verticalWall(0,0));
    auto apertureGeometry=radiationOnly(breached);
    CellAtmosphereTransport breachedRoom(apertureGeometry,CellThermalMode::LocalStructure);
    advance(breachedRoom,.1);audit(breachedRoom);
    require(breachedRoom.structure().exteriorHeatJ<0 && -breachedRoom.structure().exteriorHeatJ<.1*6*5.670374419e-8*std::pow(293.15,4),"Static breach radiation missing or exceeds black aperture bound");
    // Unresolved gas is retained, not replaced by an invented solid/EOS capacity.
    auto trace=empty;trace.mesh.cells[0].initial=station::makeCellFluidState({0,1e-25,0,0},293.15);
    CellAtmosphereTransport unresolved(trace,CellThermalMode::LocalStructure);const auto conserved=unresolved.fluid().states()[0].q;
    advance(unresolved,0.1);require(unresolved.fluid().states()[0].q==conserved,"Unresolved inventory was discarded");audit(unresolved);
    // Repressurisation via a finite warm neighbouring Cell, no infinite reservoir.
    I pair(2,3);pair.setHullType(I::HullType::BareSkin);pair.setPartitionType(I::PartitionType::BarePartition);
    for (unsigned y=0;y<3;++y) pair.toggleWall(0,pair.level(0).verticalWall(1,y));
    const auto wall=pair.level(0).verticalWall(1,1);pair.setDoor(0,wall,true);
    // Binary Door poses retain the exact bodies through repeated controls,
    // including exterior orientation reversal and same-Sector endpoints.
    auto poseCheck=[&](const I& installation,std::size_t doorWall,bool exterior) {
        auto g=radiationOnly(installation);
        if(!exterior) for(const auto& a:g.structure->assemblies) if(a.id.part==StructuralPart::DoorLeaf)
            for(unsigned side=0;side<2;++side) {
                auto& b=g.structure->bodies[a.bodies[side]];b=station::ThermalBody({b.capacityJPerK(),side ? 200. : 350.});
            }
        const auto authored=installation.serialize();
        CellAtmosphereTransport closed(g,CellThermalMode::LocalStructure),opened(g,CellThermalMode::LocalStructure);
        const auto bodies=opened.structure().bodies;
        tracking=true;
        for(int i=0;i<100;++i) {opened.setDoorOpen(0,doorWall,true);opened.setDoorOpen(0,doorWall,false);}
        tracking=false;
        require(allocations==0,"Door actuation allocated");sameBodies(opened.structure().bodies,bodies);
        require(opened.elapsedSeconds()==0,"Door control advanced time");
        // A closed pose after cycles must reproduce the original thermal map,
        // not just retain the same total energy.
        advance(closed,.001);advance(opened,.001);
        sameBodies(opened.structure().bodies,closed.structure().bodies);
        const auto cycled=opened.structure().bodies;
        opened.setDoorOpen(0,doorWall,true);
        sameBodies(opened.structure().bodies,cycled);
        require(opened.elapsedSeconds()==.001 && installation.serialize()==authored,"Door control advanced time or authored geometry");
        advance(closed,.001);advance(opened,.001);audit(closed);audit(opened);
        if(exterior) {
            const auto loss=-opened.structure().exteriorHeatJ;
            const auto black=.001*1.8*5.670374419e-8*std::pow(293.15,4);
            require(loss>black*.7 && loss<black,"Door portal is missing, Cd-adjusted, or exceeds clear area");
            require(std::abs(closed.structure().exteriorHeatJ)<1e-10,"Closed Door retained aperture");
        } else require(std::abs(opened.structure().exteriorHeatJ)<1e-10,"Interior Door became space sink");
        const auto after=opened.structure().bodies;
        opened.setDoorOpen(0,doorWall,false);sameBodies(opened.structure().bodies,after);
        opened.setDoorOpen(0,doorWall,true);
        require(!opened.advance(.1,{},4096,0),"Door refusal fixture accepted");
        sameBodies(opened.structure().bodies,after);
        advance(opened,.001);audit(opened);
    };
    poseCheck(pair,wall,false);
    // Independently exercise cavity and gas ownership: both leaf sides must
    // see the canonical (negative-x, warm) Interior endpoint when parked.
    for(bool gasOnly:{false,true}) {
        auto g=expandCellAtmosphere(pair,true);g.structure->contacts.clear();
        std::size_t positiveLeaf=SIZE_MAX;
        for(const auto& a:g.structure->assemblies) {
            for(unsigned side=0;side<2;++side) {
                const auto owner=a.owners[side];
                auto& b=g.structure->bodies[a.bodies[side]];
                b=station::ThermalBody({b.capacityJPerK(),owner && owner->worldX==0 ? 400. : 200.});
                if(a.id.part==StructuralPart::DoorLeaf) {b=station::ThermalBody({b.capacityJPerK(),300});if(side==1) positiveLeaf=a.bodies[side];}
            }
        }
        for(auto& a:g.structure->assemblies) {
            if(gasOnly) {a.material.firstSide.emissivity=0;a.material.secondSide.emissivity=0;}
            else if(!a.owners[0] || !a.owners[1]) a.material.secondSide.emissivity=0;
        }
        for(std::size_t i=0;i<g.mesh.cells.size();++i) g.mesh.cells[i].initial=gasOnly
            ? station::makeCellFluidState({0,1,0,0},g.gasOwners[i].worldX==0 ? 400 : 200) : station::CellFluidState{};
        CellAtmosphereTransport shut(g,CellThermalMode::LocalStructure),parked(g,CellThermalMode::LocalStructure);
        parked.setDoorOpen(0,wall,true);advance(shut,.001);advance(parked,.001);
        require(positiveLeaf!=SIZE_MAX && shut.structure().bodies[positiveLeaf].temperatureK()<300
            && parked.structure().bodies[positiveLeaf].temperatureK()>300,"Open leaf did not park both sides at deterministic endpoint");
        audit(shut);audit(parked);
    }
    auto noPartition=pair;noPartition.setPartitionType(std::nullopt);
    poseCheck(noPartition,wall,false);
    auto transparent=radiationOnly(noPartition);
    std::size_t coldPanel=SIZE_MAX;
    for(const auto& a:transparent.structure->assemblies) for(unsigned side=0;side<2;++side) if(a.owners[side]) {
        auto& b=transparent.structure->bodies[a.bodies[side]];
        const bool cold=a.owners[side]->worldX==1;
        b=station::ThermalBody({b.capacityJPerK(),cold ? 200. : 400.});
        if(cold) coldPanel=a.bodies[side];
    }
    CellAtmosphereTransport absentLeaf(transparent,CellThermalMode::LocalStructure);
    absentLeaf.setDoorOpen(0,wall,true);advance(absentLeaf,.001);audit(absentLeaf);
    require(coldPanel!=SIZE_MAX && absentLeaf.structure().bodies[coldPanel].temperatureK()>200,
        "Open unmodelled partition Door failed to activate portal");
    auto voidYaml=pair.serialize();const auto earth=voidYaml.find("Earth-like");
    require(earth!=std::string::npos,"Missing atmosphere fixture");voidYaml.replace(earth,10,"Void");
    I unmodelledEndpoint;unmodelledEndpoint.deserialize(voidYaml);
    poseCheck(unmodelledEndpoint,wall,false);
    for(unsigned x:{0u,1u}) {
        I outside(1,3);outside.setHullType(I::HullType::BareSkin);
        const auto door=outside.level(0).verticalWall(x,1);outside.setDoor(0,door,true);
        poseCheck(outside,door,true);
    }
    I same(2,5);same.setPartitionType(I::PartitionType::BarePartition);
    for(unsigned y=1;y<4;++y) same.toggleWall(0,same.level(0).verticalWall(1,y));
    const auto sameWall=same.level(0).verticalWall(1,2);same.setDoor(0,sameWall,true);
    require(expandStructuralTopology(same).cavityCount==1,"Same-Sector Door fixture split cavity");
    poseCheck(same,sameWall,false);
    I tallDoor(1,3);tallDoor.addLevel(1,3,2,0);tallDoor.setSector(0,0,"Tall",2);
    const auto tallWall=tallDoor.level(0).verticalWall(0,1);tallDoor.setDoor(0,tallWall,true);
    poseCheck(tallDoor,tallWall,true);
    I tallBreach=tallDoor;tallBreach.setDoor(0,tallWall,false);
    const auto breach=expandStructuralTopology(tallBreach);
    double breachArea=0;
    for(const auto& o:breach.openings) if(!o.door && o.face.axis==0 && o.face.origin.x==0 && o.face.origin.y==1) breachArea+=o.areaM2;
    require(breachArea==12,"Tall breach used fixed hydraulic area");
    CellAtmosphereTransport tallEscape(radiationOnly(tallBreach),CellThermalMode::LocalStructure);
    advance(tallEscape,.001);audit(tallEscape);
    const auto tallBlack=.001*12*5.670374419e-8*std::pow(293.15,4);
    require(-tallEscape.structure().exteriorHeatJ>tallBlack*.6 && -tallEscape.structure().exteriorHeatJ<tallBlack,
        "Tall breach radiative loss did not use physical height");
    auto warm=expandCellAtmosphere(pair,true);
    for (std::size_t i=1;i<warm.mesh.cells.size();i+=2) warm.mesh.cells[i].initial={};
    auto cold=warm;
    for (auto& body:cold.structure->bodies) body=station::ThermalBody({body.capacityJPerK(),250});
    CellAtmosphereTransport warmRoom(warm,CellThermalMode::LocalStructure),coldRoom(cold,CellThermalMode::LocalStructure);
    require(warmRoom.setDoorOpen(0,wall,true) && coldRoom.setDoorOpen(0,wall,true),"Repressurisation Door refused");
    advance(warmRoom,0.1);advance(coldRoom,0.1);
    require(coldRoom.fluid().primitives()[3].resolvedGas && coldRoom.fluid().primitives()[3].temperatureK<warmRoom.fluid().primitives()[3].temperatureK,"Chilled structure did not cool admitted gas");
    audit(warmRoom);audit(coldRoom);
    // Whole-call budget applies across source intervals. Early, late and
    // nonlinear/refinement refusals retain last committed observations at 1x/10x.
    for (const double duration:{0.1,1.0}) {
        auto geometry=expandCellAtmosphere(pair,true);
        geometry.atmospheres[0].occupants=1;geometry.atmospheres[0].oxygenUsePerOccupantKgPerS=0.001;
        CellAtmosphereTransport trial(geometry,CellThermalMode::LocalStructure);
        require(trial.setDoorOpen(0,wall,true),"Door setup failed");advance(trial,0.1);
        const auto states=std::vector<station::CellFluidState>(trial.fluid().states().begin(),trial.fluid().states().end());
        const auto bodies=trial.structure().bodies;const auto ledgers=std::vector<CellSectorAtmosphere>(trial.sectors().begin(),trial.sectors().end());
        const auto observations=std::vector<station::CellFluidState>(trial.faceTransfers().begin(),trial.faceTransfers().end());
        const auto time=trial.elapsedSeconds();const auto boundary=trial.structure().exteriorHeatJ;const auto energy=trial.structure().currentEnergyJ;
        const auto work=trial.structure().work.work;const auto momentum=trial.momentum();
        auto restored=[&] {
            require(trial.elapsedSeconds()==time && trial.structure().exteriorHeatJ==boundary && trial.structure().currentEnergyJ==energy && trial.structure().work.work==work,"Refusal committed time/ledger/observations");
            sameBodies(trial.structure().bodies,bodies);
            for (std::size_t i=0;i<states.size();++i) require(trial.fluid().states()[i].q==states[i].q,"Gas rollback failed");
            for (std::size_t i=0;i<ledgers.size();++i) require(trial.sectors()[i].ledger==ledgers[i].ledger,"Source/phase ledger rollback failed");
            for (std::size_t i=0;i<observations.size();++i) require(trial.faceTransfers()[i].q==observations[i].q,"Last flux observation changed on refusal");
            require(trial.momentum()==momentum,"Momentum rollback failed");
            require(trial.fluid().mesh().faces[trial.geometry().doors[0].face].openFraction>0,"Accepted Door control rolled back");
        };
        require(!trial.advance(duration,{},4096,0),"Early budget refusal missing");restored();
        require(!trial.advance(duration,{},4096,1),"Nonlinear budget refusal missing");restored();
        const std::vector<double> impossible(states.size(),-1e100);
        require(!trial.advance(duration,impossible),"Late heat refusal missing");restored();
        if (duration==1) {
            // Measure the exact next accepted slice on an independent replay.
            // Its budget permits the first interval, then refuses a later one.
            CellAtmosphereTransport witness(geometry,CellThermalMode::LocalStructure);
            witness.setDoorOpen(0,wall,true);advance(witness,0.1);advance(witness,0.1);
            const auto oneSliceBudget=witness.structure().work.work;
            require(!trial.advance(duration,{},4096,oneSliceBudget),"Thermal budget reset across slices");restored();
        }
        tracking=true;const bool retried=trial.advance(duration);tracking=false;
        require(retried && allocations==0,"Successful accelerated retry failed/allocated");audit(trial);
    }
    I disabled(1,1);disabled.setHullType(std::nullopt);
    CellAtmosphereTransport noHull(disabled,CellThermalMode::LocalStructure);
    require(noHull.structure().bodies.empty(),"No-Hull opt-out acquired solids");advance(noHull,0.1);audit(noHull);
    auto missing=expandCellAtmosphere(room,true);missing.atmospheres[0].reservoirId="missing";
    bool refused=false;try {CellAtmosphereTransport invalid(missing,CellThermalMode::LocalStructure);} catch (const std::invalid_argument&) {refused=true;}
    require(refused,"Unresolved reservoir accepted");
    // Void atmosphere removes gas only, not the physical structure.
    auto yaml=room.serialize();std::size_t pos=0;
    while ((pos=yaml.find("Earth-like",pos))!=std::string::npos) yaml.replace(pos,10,"Void");
    room.deserialize(yaml);
    CellAtmosphereTransport unmodelled(room,CellThermalMode::LocalStructure);
    require(unmodelled.fluid().states().empty() && !unmodelled.structure().bodies.empty(),"Interior Void lost structure");
    require(unmodelled.geometry().structure->cavityCount==1,"Interior Void preset lost its radiation cavity");
    const auto initial=solidEnergy(unmodelled);advance(unmodelled,0.1);require(solidEnergy(unmodelled)<initial,"Gas-free structure did not radiate");audit(unmodelled);
}
}
int main() {try {check();} catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
