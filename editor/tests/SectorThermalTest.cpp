#include "SectorThermal.h"
#include "SimulationPlayback.h"
#include <nsi/systems/Atmosphere.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using I=nsi::core::Installation;
using namespace station::editor;
namespace {
void require(bool v,const char* message) {if (!v) throw std::runtime_error(message);}
void near(double a,double b) {require(std::abs(a-b)<1e-8,"Incorrect thermal readout");}
void check() {
    I single(1,1);single.setHullType(I::HullType::MultiLayerBlanket);
    auto a=sectorThermal(single,0,0);
    near(a.skin.sideM2,24);near(a.skin.roofM2,4);near(a.skin.floorUndersideM2,4);
    require(a.hasAtmosphere && !a.atmosphere,"Unstarted gas telemetry fabricated");
    SimulationPlayback playback;playback.reset(single);
    auto snapshot=playback.runtime()->snapshot();
    near(sectorThermal(single,0,0,&snapshot).atmosphere->temperatureC,20);
    playback.togglePlaying();for (int n=0;n<10;++n) playback.update(.1);
    snapshot=playback.runtime()->snapshot();a=sectorThermal(single,0,0,&snapshot);
    require(a.atmosphere && *a.atmosphere==snapshot.atmospheres[0],"Thermal stats differ from runtime snapshot");
    require(a.atmosphere->temperatureC<20 && a.atmosphere->lastConductedKW<0,"Live gas did not show local surface cooling");
    require(snapshot.thermalLinks.empty() && !snapshot.thermal.hull,"Native playback exposed legacy topology");
    playback.reset(single);snapshot=playback.runtime()->snapshot();
    near(sectorThermal(single,0,0,&snapshot).atmosphere->temperatureC,20);
    require(!sectorThermal(single,0,0).atmosphere,"Removed snapshot left stale stats");

    I pair(2,1);pair.toggleWall(0,pair.level(0).verticalWall(1,0));
    auto expansion=nsi::systems::expandCellAtmosphere(pair);
    const auto mass=expansion.mesh.cells[0].initial.q;
    expansion.mesh.cells[0].initial=station::makeCellFluidState({mass[0],mass[1],mass[2],mass[3]},313.15);
    CellPlaybackRuntime runtime(pair,std::move(expansion),{},std::nullopt);
    require(runtime.update(.1),"Local thermal runtime failed");snapshot=runtime.snapshot();
    std::reverse(snapshot.atmospheres.begin(),snapshot.atmospheres.end());
    const auto left=pair.level(0).cellData(0,0).sector,right=pair.level(0).cellData(1,0).sector;
    const auto l=sectorThermal(pair,0,left,&snapshot),r=sectorThermal(pair,0,right,&snapshot);
    require(l.atmosphere && r.atmosphere && l.atmosphere->id!=r.atmosphere->id && l.atmosphere->temperatureC>r.atmosphere->temperatureC,"Sector selection ignored stable atmosphere identity");
    require(l.atmosphere->id==nsi::systems::expandSector(pair,0,left)->id,"Wrong selected gas identity");
    auto missing=snapshot;missing.atmospheres.clear();
    require(!sectorThermal(pair,0,left,&missing).atmosphere,"Missing compartment fabricated stats");
    auto yaml=pair.serialize();
    for (auto p=yaml.find("Earth-like");p!=std::string::npos;p=yaml.find("Earth-like")) yaml.replace(p,10,"Void");
    I vacuum;vacuum.deserialize(yaml);
    require(!sectorThermal(vacuum,0,0,&snapshot).hasAtmosphere && !sectorThermal(vacuum,0,0,&snapshot).atmosphere,"Void preset reused stale gas stats");
    vacuum.setCell(0,0,0,I::SectorType::Void);
    require(!sectorThermal(vacuum,0,vacuum.level(0).cellData(0,0).sector,&snapshot).atmosphere,"Void geometry gained gas stats");
}
}
int main() {try {check();} catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
