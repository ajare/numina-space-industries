#include "StructuralPanel.h"
#include "InstallationRenderMode.h"
#include "SectorThermal.h"
#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace station::editor;
using I=nsi::core::Installation;
void require(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void observations() {
    I scene(4,4);scene.setHullType(I::HullType::BareSkin);scene.setOffset(0,-3,7);
    const auto authored=scene.serialize();
    SimulationPlayback p(std::nullopt);p.reset(scene);
    const nsi::systems::StructuralOwner owner{0,-3,7};
    const auto cavity=structuralCavity(*p.runtime(),owner);
    require(cavity.has_value(),"Offset Cell did not find its cavity");
    require(structuralCavity(*p.runtime(),{0,0,10})==cavity,"Non-sorted Cell owner lookup failed");
    require(!structuralCavity(*p.runtime(),{0,100,100}),"Unknown owner mapped to cavity");
    auto graph=structuralGraph(*p.runtime(),owner,cavity,true);
    require(graph.surfaceCount.written==16 && graph.surfaceCount.total>16,"Sector topology is not bounded");
    auto next=structuralGraph(*p.runtime(),owner,cavity,true,1);
    require(next.surfaceCount.written && next.surfaces[0].id!=graph.surfaces[0].id,"Surface pagination repeated page");
    require(graph.contactCount.written && graph.contactCount.written<=16,"Immediate contact topology missing/unbounded");
    for (std::size_t i=0;i<graph.contactCount.written;++i)
        require(graph.contacts[i].a==graph.surfaces[0].id || graph.contacts[i].b==graph.surfaces[0].id,"Contact edge is not incident to selection");
    graph=structuralGraph(*p.runtime(),owner,cavity,false);
    require(graph.surfaceCount.written==4,"Corner Cell query used Sector or wrong world coordinates");
    require(p.structuralHistory(*cavity).size()==1 && std::abs(p.structuralHistory(*cavity)[0].meanC-20)<1e-9,"Reset history missing initialization");
    InstallationRenderSettings settings;settings.mode=InstallationRenderMode::Structure;
    auto snapshot=p.runtime()->snapshot();
    const auto colours=cellRenderColours(scene,0,settings,&snapshot,&p.runtime()->model());
    require(colours[0]==*thermalColour(20,settings.thermal),"Structure render did not use surfaces");
    snapshot.cellAtmospheres[0].resolvedGas=false;snapshot.cellAtmospheres[0].temperatureC=-200;
    settings.mode=InstallationRenderMode::Thermal;
    require(cellRenderColours(scene,0,settings,&snapshot,&p.runtime()->model())[0]==unresolvedGasColour(),"Unresolved gas rendered as cold");
    settings.mode=InstallationRenderMode::Structure;
    require(cellRenderColours(scene,0,settings,&snapshot,&p.runtime()->model())[0]==colours[0],"Gas measurement replaced surface colour");
    settings.mode=InstallationRenderMode::Pressure;
    require(cellRenderColours(scene,0,settings,&snapshot,&p.runtime()->model())[0]==*pressureColour(snapshot.cellAtmospheres[0].pressureKPa,settings.pressure),"Pressure mode changed");
    const auto thermal=sectorThermal(scene,0,0,&snapshot);
    require(thermal.atmosphere.has_value(),"Local gas thermal observations missing");
    p.togglePlaying();p.setSpeedIndex(4);p.update(.1);
    require(p.structuralHistory(*cavity).size()==2 && p.structuralHistory(*cavity).back().seconds==1,"History uses frame instead of accelerated simulation time");
    require(p.structuralHistory(*cavity).back().minimumC<p.structuralHistory(*cavity).back().maximumC,"History lost structural gradients");
    p.togglePlaying();p.update(1);require(p.structuralHistory(*cavity).size()==2,"Pause added history");
    p.setObservationOptions({false,false});
    require(p.structuralHistory(*cavity).size()==2 && p.runtime()->structuralCell(owner).all.surfaces,"Optional wind/force disabled structural observations");
    p.reset(scene);require(p.structuralHistory(*cavity).size()==1 && p.structuralHistory(*cavity).back().seconds==0,"Reset retained history");
    require(scene.serialize()==authored,"Inspection/playback mutated save data");
    scene.setHullType(std::nullopt);p.reset(scene);
    require(p.structuralHistory(*cavity).empty() && p.runtime()->structuralSnapshot().bodies.empty(),"No-Hull Reset retained solid history");
    require(cellRenderColours(scene,0,settings,nullptr).size()==scene.level(0).cellCount(),"Missing runtime failed render fallback");
    p.synchronize(nullptr);require(!p.runtime() && p.structuralHistory(*cavity).empty(),"Retired document retained structural history");
    I optOut(1,1);optOut.setHullType(std::nullopt);p.reset(optOut);
    settings.mode=InstallationRenderMode::Structure;
    const auto absent=cellRenderColours(optOut,0,settings,nullptr,&p.runtime()->model());
    require(absent[0]==unresolvedGasColour(),"Absent solid measurement fabricated a surface temperature");
    require(p.runtime()->structuralCell({0,0,0}).all.surfaces==0 && p.structuralHistory(0).empty(),"No-Hull opt-out gained thermal structure/history");
    const auto missing=structuralSample(4,{});
    require(std::isnan(missing.meanC) && std::isnan(missing.floorC) && std::isnan(missing.minimumC),"Unavailable history fabricated zero temperature");
}
void missingGasAndRefusal() {
    I scene(1,1);auto yaml=scene.serialize();auto pos=yaml.find("atmosphere: Earth-like");
    require(pos!=std::string::npos,"Missing preset fixture");yaml.replace(pos,22,"atmosphere: Void");scene.deserialize(yaml);
    SimulationPlayback p(std::nullopt);p.reset(scene);
    auto graph=structuralGraph(*p.runtime(),{0,0,0},0,false);
    require(graph.surfaceCount.written==6 && !graph.surfaces[0].gasTemperatureK,"Missing gas deleted surfaces or invented temperature");
    require(p.structuralHistory(0).size()==1,"No-gas Sector omitted from surface history");
    p.togglePlaying();p.update(.1);require(p.structuralHistory(0).size()==2,"No-gas structure stopped evolving");
    const std::string failure=R"lua(return function(name)
        return {advance=function(self,tick)
            if name=='ECLSS' and tick==2 then error('intentional refusal') end
            return tick
        end}
    end)lua";
    I room(1,1);SimulationPlayback refused(failure);refused.reset(room);refused.togglePlaying();refused.update(.1);
    const auto committed=refused.structuralHistory(0).back();
    const auto rates=refused.runtime()->structuralSnapshot().rates;
    for (int attempt=0;attempt<2;++attempt) {
        bool failed=false;try {refused.update(attempt ? 0 : .1);} catch (const std::runtime_error&) {failed=true;}
        require(failed && !refused.playing(),"Refusal failed to pause");
        require(refused.structuralHistory(0).size()==2 && refused.structuralHistory(0).back().seconds==committed.seconds
            && refused.structuralHistory(0).back().meanC==committed.meanC,"Refused trial published surface history");
        require(refused.runtime()->structuralSnapshot().rates==rates,"Refusal changed displayed heat rates");
        if (!attempt) refused.togglePlaying();
    }
    // Thermal (rather than lifecycle) failure retains graph endpoint measurements.
    CellPlaybackRuntime runtime(room,{},std::nullopt);require(runtime.update(.1),"Thermal fixture setup failed");
    const auto before=structuralGraph(runtime,{0,0,0},0,false);
    require(!runtime.update(1,4096,0),"Thermal work refusal not triggered");
    const auto after=structuralGraph(runtime,{0,0,0},0,false);
    require(before.surfaces[0].temperatureK==after.surfaces[0].temperatureK,"Refusal changed topology inspection values");
}
void doorAndRollingHistory() {
    I scene(3,3);scene.setHullType(I::HullType::BareSkin);
    const auto door=scene.level(0).horizontalWall(1,0);scene.setDoor(0,door,true);
    SimulationPlayback p(std::nullopt);p.reset(scene);p.togglePlaying();p.update(.1);
    const auto size=p.structuralHistory(0).size();
    const auto seconds=p.structuralHistory(0).back().seconds;
    const auto energy=p.runtime()->structuralSnapshot().solidEnergyJ;
    require(p.toggleDoor(0,door),"Door control unavailable during structural playback");
    const auto cavity=structuralCavity(*p.runtime(),{0,1,0});
    auto graph=structuralGraph(*p.runtime(),{0,1,0},cavity,false);
    bool opening=false;
    for (std::size_t i=0;i<graph.portalCount.written;++i) {
        const auto& portal=graph.portals[i];
        if (portal.areaM2>0) {
            opening=true;require(std::abs(portal.areaM2-1.8)<1e-12,"Door graph used gas Cd area");
            require(!portal.outwardPowerW,"Door graph showed stale power after control");
        }
    }
    require(opening && p.structuralHistory(0).size()==size && p.structuralHistory(0).back().seconds==seconds,
        "Door actuation advanced history or omitted aperture");
    require(p.runtime()->structuralSnapshot().solidEnergyJ==energy,"Door inspection/control reset energy");
    require(p.toggleDoor(0,door),"Door close failed");
    p.update(.1);graph=structuralGraph(*p.runtime(),{0,1,0},cavity,false);
    for (std::size_t i=0;i<graph.portalCount.written;++i) require(graph.portals[i].areaM2==0,"Closed Door retained radiation aperture");
    I tiny(1,1);p.reset(tiny);p.setSpeedIndex(0);p.togglePlaying();
    for (int i=0;i<9;++i) p.update(.1);
    require(p.structuralHistory(0).size()==1,"Slow frames oversampled structural history");
    p.update(.1);require(p.structuralHistory(0).size()==2,"Simulation-time sampling lost slow update");
    p.setSpeedIndex(4);
    for (int i=0;i<62;++i) p.update(.1);
    const auto history=p.structuralHistory(0);
    require(history.size()<=601 && history.front().seconds>=history.back().seconds-60-1e-9,"Surface history is not bounded to one simulated minute");
}
void ui() {
    ImGui::CreateContext();ImPlot::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1400,2400};io.DeltaTime=1.f/60;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    I scene(2,2);SimulationPlayback p(std::nullopt);p.reset(scene);p.togglePlaying();p.update(.1);p.togglePlaying();
    StructuralPanelState view;
    for (bool sector:{false,true}) for (int frame=0;frame<2;++frame) {
        view.sector=sector;view.page=0;
        ImGui::NewFrame();ImGui::SetNextWindowSize({1200,2200});ImGui::Begin("Structure inspector");
        drawStructuralPanel(p,{0,0,0},view);
        auto* plot=ImPlot::GetPlot("###StructuralHistory");
        require(plot && plot->Items.GetLegendCount()==6,"Surface UI omitted directional/min/max histories");
        require(plot->Axes[ImAxis_X1].Range.Max==60,"History axis not simulation-time window");
        ImGui::End();ImGui::Render();require(ImGui::GetDrawData()->TotalVtxCount>0,"Structural UI failed render");
    }
    // Selection change resets stale pagination and side selection.
    view.page=20;view.surface=15;
    ImGui::NewFrame();ImGui::Begin("Structure inspector");drawStructuralPanel(p,{0,1,1},view);ImGui::End();ImGui::Render();
    require(view.page==0 && view.surface==0,"New Cell retained stale page/side selection");
    scene.setHullType(std::nullopt);p.reset(scene);
    ImGui::NewFrame();ImGui::Begin("Structure inspector");drawStructuralPanel(p,{0,0,0},view);ImGui::End();ImGui::Render();
    ImPlot::DestroyContext();ImGui::DestroyContext();
}
int main() {
    try {observations();missingGasAndRefusal();doorAndRollingHistory();ui();}
    catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
