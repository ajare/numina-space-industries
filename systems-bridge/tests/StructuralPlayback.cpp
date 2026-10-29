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
    // Unresolved gas is retained, not replaced by an invented solid/EOS capacity.
    auto trace=empty;trace.mesh.cells[0].initial=station::makeCellFluidState({0,1e-25,0,0},293.15);
    CellAtmosphereTransport unresolved(trace,CellThermalMode::LocalStructure);const auto conserved=unresolved.fluid().states()[0].q;
    advance(unresolved,0.1);require(unresolved.fluid().states()[0].q==conserved,"Unresolved inventory was discarded");audit(unresolved);
    // Repressurisation via a finite warm neighbouring Cell, no infinite reservoir.
    I pair(2,3);pair.setHullType(I::HullType::BareSkin);pair.setPartitionType(I::PartitionType::BarePartition);
    for (unsigned y=0;y<3;++y) pair.toggleWall(0,pair.level(0).verticalWall(1,y));
    const auto wall=pair.level(0).verticalWall(1,1);pair.setDoor(0,wall,true);
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
    const auto initial=solidEnergy(unmodelled);advance(unmodelled,0.1);require(solidEnergy(unmodelled)<initial,"Gas-free structure did not radiate");audit(unmodelled);
}
}
int main() {try {check();} catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
