#include "CellPlaybackRuntime.h"
#include "StructuralView.h"
#include <iostream>
#include <stdexcept>
using namespace station::editor;
using namespace nsi::systems;
using I=nsi::core::Installation;
namespace {
void require(bool value,const char* why) {if(!value) throw std::runtime_error(why);}
void same(const CellPlaybackRuntime& a,const CellPlaybackRuntime& b) {
    require(a.model().elapsedSeconds()==b.model().elapsedSeconds(),"Inspection changed physical time");
    const auto as=a.model().fluid().states(),bs=b.model().fluid().states();
    require(as.size()==bs.size(),"Initial expansion changed gas count");
    for(std::size_t i=0;i<as.size();++i) require(as[i].q==bs[i].q,"Inspection/constructor changed gas evolution");
    const auto& x=a.structuralSnapshot();const auto& y=b.structuralSnapshot();
    require(x.bodies.size()==y.bodies.size(),"Initial expansion changed body count");
    for(std::size_t i=0;i<x.bodies.size();++i) require(x.bodies[i].energyJ()==y.bodies[i].energyJ(),"Inspection changed structural evolution");
    require(x.paths==y.paths && x.work.work==y.work.work && x.residualJ==y.residualJ,"Inspection changed heat/error/work bounds");
}
void inspect(const CellPlaybackRuntime& runtime) {
    const auto owner=runtime.model().geometry().gasOwners.back();
    runtime.structuralCell(owner);runtime.structuralSector(0);
    structuralGraph(runtime,owner,0,true);structuralGraph(runtime,owner,0,false);
}
void check() {
    I scene(8,8);scene.setHullType(I::HullType::BareSkin);const auto authored=scene.serialize();
    CellPlaybackRuntime legacy(scene);require(legacy.model().thermalMode()==CellThermalMode::Geometry,"Unapproved default rollout");
    constexpr auto mode=CellThermalMode::LocalStructure;
    for(double duration:{.1,1.}) {
        CellPlaybackRuntime ordinary(scene,{true,true},std::nullopt,mode);
        CellPlaybackRuntime explicitReset(scene,expandCellAtmosphere(scene,true),{true,true},std::nullopt,mode);
        same(ordinary,explicitReset);
        const auto initial=ordinary.structuralSnapshot().solidEnergyJ;
        for(int i=0;i<3;++i) {
            inspect(explicitReset);
            require(ordinary.update(duration) && explicitReset.update(duration),"Closed no-breach advance refused");
            same(ordinary,explicitReset);
        }
        require(ordinary.structuralSnapshot().solidEnergyJ<initial,"No-breach evolution skipped");
        require(ordinary.model().elapsedSeconds()==3*duration,"Idle path lost time");
        // Both remain geometric Interior rooms, with retained solids but no gas.
        auto empty=expandCellAtmosphere(scene,true);
        for(auto& c:empty.mesh.cells) c.initial={};
        auto& cold=empty.structure->bodies.front();cold=station::ThermalBody({cold.capacityJPerK(),250});
        CellPlaybackRuntime unselected(scene,empty,{true,true},std::nullopt,mode);
        CellPlaybackRuntime selected(scene,std::move(empty),{true,true},std::nullopt,mode);
        inspect(selected);const auto coldEnergy=selected.structuralSnapshot().bodies.front().energyJ();
        require(unselected.update(duration) && selected.update(duration),"Evacuated advance refused");
        same(unselected,selected);
        require(selected.model().work().resting && selected.model().work().substeps==0,"Vacuum acoustic rest shortcut lost");
        require(selected.structuralSnapshot().work.intervals>0 && selected.structuralSnapshot().bodies.front().energyJ()!=coldEnergy,
            "Acoustic rest skipped connected active solids");
    }
    require(scene.serialize()==authored,"Experiment mutated authoring");
}
}
int main() {try {check();} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
