#include "SectorThermal.h"
#include "SimulationPlayback.h"
#include <station/StationSystemsRuntime.h>
#include <nsi/systems/Atmosphere.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using I = nsi::core::Installation;
using namespace station::editor;
namespace {
void require(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
void near(double a, double b) { require(std::abs(a-b)<1e-8, "Incorrect thermal readout"); }
void check() {
    I single(1,1);
    single.setHullType(I::HullType::MultiLayerBlanket);
    auto a = sectorThermal(single,0,0);
    near(a.skin.sideM2,24); near(a.skin.roofM2,4); near(a.skin.floorUndersideM2,4);
    require(a.links.size()==1 && a.links[0].hull && !a.isolated(), "Missing Hull-only graph");
    near(a.links[0].conductanceKWPerK,0.05*32/1000);
    near(*a.drainSeconds,0.851*12/(0.05*32/1000));
    require(!a.links[0].outwardKW, "Unstarted flow fabricated");
    require(!a.atmosphere, "Live atmosphere fabricated without a snapshot");
    I bigger(2,2); bigger.setHullType(single.hullType());
    auto b = sectorThermal(bigger,0,0);
    require(*b.drainSeconds>*a.drainSeconds, "Timescale ignored actual geometry");
    SimulationPlayback playback;
    playback.synchronize(single);
    auto initial = playback.runtime()->snapshot();
    auto initialReadout = sectorThermal(single,0,0,&initial);
    require(initialReadout.atmosphere.has_value(), "Initial atmosphere stats missing");
    near(initialReadout.atmosphere->temperatureC,20);
    near(initialReadout.atmosphere->startingTemperatureC,20);
    playback.togglePlaying();
    for (int n=0;n<10;++n) playback.update(0.1);
    auto snapshot = playback.runtime()->snapshot();
    a=sectorThermal(single,0,0,&snapshot);
    require(a.links[0].outwardKW && *a.links[0].outwardKW>0, "Live Hull outflow missing");
    near(*a.links[0].outwardKW,snapshot.thermalLinks[0].flowRateKW);
    require(a.atmosphere && *a.atmosphere==snapshot.atmospheres[0], "Thermal stats differ from runtime snapshot");
    require(a.atmosphere->temperatureC < a.atmosphere->startingTemperatureC,
        "Live temperature did not show Hull cooling");
    require(a.atmosphere->currentEnergyKJ < a.atmosphere->startingEnergyKJ
        && a.atmosphere->lastConductedKW < 0, "Live thermal energy and cooling rate missing");
    playback.reset(single);
    auto reset = playback.runtime()->snapshot();
    near(sectorThermal(single,0,0,&reset).atmosphere->temperatureC,20);
    require(!sectorThermal(single,0,0).atmosphere, "Removed snapshot left stale atmosphere stats");

    I pair(2,1);
    pair.setHullType(I::HullType::BareSkin);
    pair.setPartitionType(I::PartitionType::BarePartition);
    pair.toggleWall(0,pair.level(0).verticalWall(1,0));
    auto left=pair.level(0).cellData(0,0).sector, right=pair.level(0).cellData(1,0).sector;
    // Real runtime with one warm endpoint proves both endpoint orientations.
    station::StationSystemsSettings settings;
    settings.enabled=true;
    settings.systemsStepWorldTicks=1;
    settings.atmospheres=nsi::systems::expandInstallation(pair);
    settings.atmospheres[0].startingTemperatureC=40;
    auto thermal=nsi::systems::expandThermal(pair);
    settings.hull=thermal.hull; settings.thermalLinks=thermal.links; settings.solarCycle=thermal.solarCycle;
    station::StationSystemsRuntime runtime(settings, 0.1f);
    require(runtime.advanceTo(1), "Thermal runtime failed");
    snapshot=runtime.snapshot();
    require(snapshot.healthy && snapshot.elapsedSeconds > 0, "Thermal runtime did not advance");
    // Snapshot order is not Sector order: select by stable id, not index/name.
    std::reverse(snapshot.atmospheres.begin(),snapshot.atmospheres.end());
    auto l=sectorThermal(pair,0,left,&snapshot), r=sectorThermal(pair,0,right,&snapshot);
    require(l.atmosphere && r.atmosphere && l.atmosphere->id!=r.atmosphere->id,
        "Sector selection did not resolve distinct atmosphere stats");
    require(l.atmosphere->temperatureC > r.atmosphere->temperatureC,
        "Selected temperature came from wrong compartment");
    for (const auto* state : {&l,&r}) {
        const auto expected=nsi::systems::expandSector(pair,0,state==&l ? left : right);
        require(state->atmosphere->id==expected->id,"Readout ignored stable atmosphere identity");
        const auto found=std::find_if(snapshot.atmospheres.begin(),snapshot.atmospheres.end(),
            [&](const auto& air){ return air.id==expected->id; });
        require(found!=snapshot.atmospheres.end() && *state->atmosphere==*found,
            "Selected thermal stats do not match snapshot");
    }
    auto missing=snapshot; missing.atmospheres.clear();
    require(!sectorThermal(pair,0,left,&missing).atmosphere,
        "Missing compartment fabricated live stats");
    require(l.links.size()==2 && r.links.size()==2,"Not every attached link is shown");
    const auto neighbour=[](const SectorThermal& s)->const SectorThermalLink& {
        for (const auto& row:s.links) if (!row.hull) return row;
        throw std::runtime_error("Missing neighbour");
    };
    near(neighbour(l).conductanceKWPerK,5*6/1000.0);
    near(*neighbour(l).outwardKW,-*neighbour(r).outwardKW);
    require(std::abs(*neighbour(l).outwardKW)>0,"Neighbour flow absent");
    near(*neighbour(l).diffusionToDrainRatio,neighbour(l).timescaleSeconds / *l.drainSeconds);

    auto evacuated=snapshot;for (auto& air:evacuated.atmospheres) air.thermalCapacityKJPerK=0;
    const auto noCapacity=sectorThermal(pair,0,left,&evacuated);
    require(!neighbour(noCapacity).diffusionToDrainRatio,"Zero capacity fabricated a NaN response ratio");
    I buried(std::vector<I::Level>{I::Level(3,3),I::Level(3,3),I::Level(3,3)});
    buried.setHullType(I::HullType::BareSkin);
    buried.setPartitionType(I::PartitionType::BarePartition);
    for (auto w:{buried.level(1).horizontalWall(1,1),buried.level(1).horizontalWall(1,2),
                 buried.level(1).verticalWall(1,1),buried.level(1).verticalWall(2,1)}) buried.toggleWall(1,w);
    const auto center=buried.level(1).cellData(1,1).sector;
    auto core=sectorThermal(buried,1,center);
    near(core.skin.totalM2(),0);
    require(core.links.size()==3 && !core.drainSeconds && !core.isolated(),"Buried neighbour-only room mislabelled");
    for (const auto& row:core.links) require(!row.hull && !row.diffusionToDrainRatio,"Buried room has drain");
    buried.setPartitionType(std::nullopt);
    require(sectorThermal(buried,1,center).isolated(),"Isolated room notice absent");
    single.setHullType(std::nullopt);
    require(sectorThermal(single,0,0).isolated(),"Legacy no-Hull room not isolated");
    auto yaml=pair.serialize();
    for (auto p=yaml.find("Earth-like");p!=std::string::npos;p=yaml.find("Earth-like")) yaml.replace(p,10,"Void");
    I vacuum; vacuum.deserialize(yaml);
    auto empty=sectorThermal(vacuum,0,0);
    require(!empty.hasAtmosphere && empty.links.empty() && !empty.isolated(),"Void compartment gained links");
    require(!sectorThermal(vacuum,0,0,&snapshot).atmosphere,
        "Void-preset Sector reused live stats from an old compartment");
    vacuum.setCell(0,0,0,I::SectorType::Void);
    require(!sectorThermal(vacuum,0,vacuum.level(0).cellData(0,0).sector,&snapshot).atmosphere,
        "Void Sector gained atmosphere stats");
}
}
int main() { try { check(); } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
