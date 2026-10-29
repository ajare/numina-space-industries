#include "InstallationRenderMode.h"
#include "InstallationRenderControls.h"
#include "DeckGridWidget.h"
#include "SimulationPlayback.h"
#include "station/editor/EditorSession.h"
#include <nsi/systems/Atmosphere.h>
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace station::editor;
using I=nsi::core::Installation;
namespace {
void require(bool v,const char* message) { if (!v) throw std::runtime_error(message); }
void near(float a,float b) { require(std::abs(a-b)<1e-6f,"Incorrect interpolated colour"); }
void colour(const std::optional<SectorColour>& actual,SectorColour expected) {
    require(actual.has_value(),"Valid temperature has no colour");
    near(actual->red,expected.red); near(actual->green,expected.green); near(actual->blue,expected.blue);
}
void scaleMapping() {
    InstallationRenderSettings settings;
    require(settings.mode==InstallationRenderMode::Default,"Default mode changed");
    auto& scale=settings.thermal;
    require(scale.minimumC==-273 && scale.maximumC==100 && scale.referenceC==20,"Wrong Celsius defaults");
    colour(thermalColour(-273,scale),{0,0,1});
    colour(thermalColour(20,scale),{0,1,0});
    colour(thermalColour(100,scale),{1,0,0});
    colour(thermalColour(-126.5,scale),{0,0.5f,0.5f});
    colour(thermalColour(60,scale),{0.5f,0.5f,0});
    colour(thermalColour(-500,scale),{0,0,1});
    colour(thermalColour(500,scale),{1,0,0});
    // Changing any anchor affects its own half of the piecewise scale.
    scale.minimumC=-20; scale.maximumC=60; scale.referenceC=0;
    colour(thermalColour(-20,scale),{0,0,1});
    colour(thermalColour(0,scale),{0,1,0});
    colour(thermalColour(60,scale),{1,0,0});
    colour(thermalColour(-10,scale),{0,0.5f,0.5f});
    colour(thermalColour(30,scale),{0.5f,0.5f,0});
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::infinity()})
        require(!thermalColour(invalid,scale),"Nonfinite temperature produced colour");
    scale.referenceC=scale.minimumC;
    require(!scale.valid() && !thermalColour(0,scale),"Degenerate scale divided by zero");
    scale.referenceC=scale.maximumC+1;
    require(!thermalColour(0,scale),"Inverted scale accepted");
    scale.referenceC=std::numeric_limits<float>::quiet_NaN();
    require(!thermalColour(0,scale),"Nonfinite scale accepted");
}
void pressureRendering() {
    InstallationRenderSettings settings;
    auto& scale=settings.pressure;
    require(scale.minimumKPa==0 && scale.referenceKPa==101.325f && scale.maximumKPa==150,"Wrong pressure defaults");
    colour(pressureColour(0,scale),{0,0,1});
    colour(pressureColour(scale.referenceKPa,scale),{0,1,0});
    colour(pressureColour(150,scale),{1,0,0});
    colour(pressureColour(scale.referenceKPa/2,scale),{0,0.5f,0.5f});
    colour(pressureColour((double(scale.referenceKPa)+150)/2,scale),{0.5f,0.5f,0});
    colour(pressureColour(1000,scale),{1,0,0});
    scale.minimumKPa=20; colour(pressureColour(0,scale),{0,0,1});
    require(!pressureColour(-1,scale) && !pressureColour(std::numeric_limits<double>::quiet_NaN(),scale),"Invalid pressure got a colour");
    scale.referenceKPa=scale.minimumKPa;
    require(!scale.valid() && !pressureColour(50,scale),"Degenerate pressure scale accepted");
    scale={}; settings.mode=InstallationRenderMode::Pressure;
    I i(std::vector<I::Level>{I::Level(1,1),I::Level(1,1)});
    SimulationPlayback p; p.reset(i); auto snapshot=p.runtime()->snapshot();
    const auto before=i;
    snapshot.atmospheres[0].pressureKPa=0; snapshot.atmospheres[1].pressureKPa=150;
    std::reverse(snapshot.atmospheres.begin(),snapshot.atmospheres.end());
    require(sectorRenderColours(i,0,settings,&snapshot)[0]==SectorColour{0,0,1},"Pressure tint used snapshot index");
    require(sectorRenderColours(i,1,settings,&snapshot)[0]==SectorColour{1,0,0},"Pressure tint used wrong Level");
    const std::vector<SectorColour> defaults{defaultSectorColour(I::SectorType::Interior)};
    require(sectorRenderColours(i,0,settings)==defaults,"Missing pressure data fabricated colours");
    auto invalid=snapshot; invalid.enabled=false;
    require(sectorRenderColours(i,0,settings,&invalid)==defaults,"Disabled pressure data used");
    invalid=snapshot; invalid.atmospheres.clear();
    require(sectorRenderColours(i,0,settings,&invalid)==defaults,"Missing pressure compartment used");
    invalid=snapshot;
    for (auto& air:invalid.atmospheres) air.pressureKPa=std::numeric_limits<double>::infinity();
    require(sectorRenderColours(i,0,settings,&invalid)==defaults,"Invalid pressure coloured Sector");
    auto yaml=i.serialize(); auto at=yaml.find("Earth-like"); yaml.replace(at,10,"Void");
    I absent; absent.deserialize(yaml);
    require(sectorRenderColours(absent,0,settings,&snapshot)==defaults,"Void preset got pressure tint");
    require(i==before,"Pressure rendering changed document");
}
void runtimeColours() {
    I installation(std::vector<I::Level>{I::Level(2,1),I::Level(1,1)});
    installation.toggleWall(0,installation.level(0).verticalWall(1,0));
    for (std::size_t l=0;l<installation.levelCount();++l)
        for (std::size_t s=0;s<installation.level(l).sectors().size();++s)
            installation.setSector(l,s,"Same name",1);
    const auto original=installation.serialize();
    station::StationSystemsSettings settings;
    settings.enabled=true; settings.systemsStepWorldTicks=1;
    settings.atmospheres=nsi::systems::expandInstallation(installation);
    settings.atmospheres[0].startingTemperatureC=-50;
    settings.atmospheres[1].startingTemperatureC=20;
    settings.atmospheres[2].startingTemperatureC=60;
    station::StationSystemsRuntime runtime(settings,0.1f);
    require(runtime.advanceTo(1),"Temperature runtime failed");
    auto snapshot=runtime.snapshot();
    std::reverse(snapshot.atmospheres.begin(),snapshot.atmospheres.end());
    InstallationRenderSettings render;
    auto defaults=sectorRenderColours(installation,0,render,&snapshot);
    for (const auto c:defaults) require(c==defaultSectorColour(I::SectorType::Interior),"Default tint changed");
    render.mode=InstallationRenderMode::Thermal;
    for (std::size_t l=0;l<installation.levelCount();++l) {
        const auto colours=sectorRenderColours(installation,l,render,&snapshot);
        for (std::size_t s=0;s<colours.size();++s) {
            const auto expected=nsi::systems::expandSector(installation,l,s);
            const auto air=std::find_if(snapshot.atmospheres.begin(),snapshot.atmospheres.end(),
                [&](const auto& state){ return state.id==expected->id; });
            require(air!=snapshot.atmospheres.end() &&
                colours[s]==*thermalColour(air->temperatureC,render.thermal),
                "Sector tint used snapshot index, name or wrong Level instead of stable identity");
        }
    }
    require(sectorRenderColours(installation,0,render)==defaults,"Missing runtime fabricated temperatures");
    auto missing=snapshot; missing.atmospheres.clear();
    require(sectorRenderColours(installation,0,render,&missing)==defaults,"Missing compartment fabricated temperature");
    missing=snapshot;
    for (auto& air:missing.atmospheres) air.temperatureC=std::numeric_limits<double>::quiet_NaN();
    require(sectorRenderColours(installation,0,render,&missing)==defaults,"Invalid live temperature changed tint");
    auto yaml=original;
    for (auto p=yaml.find("Earth-like");p!=std::string::npos;p=yaml.find("Earth-like")) yaml.replace(p,10,"Void");
    I vacuum; vacuum.deserialize(yaml);
    require(sectorRenderColours(vacuum,0,render,&snapshot)==defaults,"Void preset reused old live data");
    vacuum.setCell(0,0,0,I::SectorType::Void);
    const auto voidColours=sectorRenderColours(vacuum,0,render,&snapshot);
    require(voidColours[vacuum.level(0).cellData(0,0).sector]==defaultSectorColour(I::SectorType::Void),
        "Void space coloured as atmosphere");
    require(installation.serialize()==original,"Render modes changed authored Installation");

    EditorSession session; session.newDocument(1,1);
    const auto dirty=session.unsavedChanges();
    const auto before=session.installation();
    SimulationPlayback playback; playback.reset(session.installation());
    auto initial=playback.runtime()->snapshot();
    const auto start=sectorRenderColours(session.installation(),0,render,&initial);
    require(start[0]==SectorColour{0,1,0},"Initial reference temperature is not pure green");
    playback.setSpeedIndex(4); playback.togglePlaying();
    for (int frame=0;frame<300;++frame) playback.update(0.1);
    const auto cooled=playback.runtime()->snapshot();
    const auto coolColours=sectorRenderColours(session.installation(),0,render,&cooled);
    require(coolColours[0].blue>0 && coolColours[0].red==0,"Live cooling did not shift green toward blue");
    require(session.installation()==before && session.unsavedChanges()==dirty,"Viewing heatmap dirtied document");
    playback.reset(session.installation()); initial=playback.runtime()->snapshot();
    require(sectorRenderColours(session.installation(),0,render,&initial)==start,"Reset retained stale tints");
    render.mode=InstallationRenderMode::Default;
    require(sectorRenderColours(session.installation(),0,render,&cooled)[0]==
        defaultSectorColour(I::SectorType::Interior),"Switching back failed to restore Default");
}
void doorInput() {
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1024,768}; io.DeltaTime=1/60.0f;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    I installation(5,5); const auto door=installation.level(0).horizontalWall(2,2);
    installation.toggleWall(0,installation.level(0).horizontalWall(1,2));
    installation.toggleWall(0,installation.level(0).horizontalWall(3,2));
    ImVec2 origin;
    bool running=false;
    EditMode mode=EditMode::Door;
    std::vector<std::size_t> open;
    auto frame=[&]() {
        ImGui::NewFrame(); ImGui::SetNextWindowPos({40,40}); ImGui::SetNextWindowSize({700,600});
        ImGui::Begin("Door input"); origin=ImGui::GetCursorScreenPos();
        const auto event=drawDeckGrid("Doors",installation.level(0),mode,std::nullopt,{},running,open);
        ImGui::End(); ImGui::Render(); return event;
    };
    frame(); frame();
    io.AddMousePosEvent(origin.x+2.5f*34,origin.y+2*34); frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);
    auto event=frame();
    require(event.phase==DeckPointerPhase::Press && event.wall==door && !event.erase,
        "Door left-click did not request placement on nearest Wall");
    installation.setDoor(0,*event.wall,!event.erase);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false); frame();
    float cyanLeft=std::numeric_limits<float>::max(),cyanRight=std::numeric_limits<float>::lowest();
    const auto* geometry=ImGui::GetDrawData();
    for (int list=0;list<geometry->CmdListsCount;++list)
        for (const auto& vertex:geometry->CmdLists[list]->VtxBuffer)
            if (vertex.col==IM_COL32(70,210,225,255)) {
                cyanLeft=std::min(cyanLeft,vertex.pos.x);cyanRight=std::max(cyanRight,vertex.pos.x);
            }
    require(cyanRight>=cyanLeft && std::abs((cyanRight-cyanLeft)-34*0.9f/2)<1,
        "Door symbol does not show its 0.9 m clear width within a 2 m Wall");
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,true);
    event=frame();
    require(event.phase==DeckPointerPhase::Press && event.wall==door && event.erase,
        "Door right-click did not request erasure");
    installation.setDoor(0,*event.wall,!event.erase);
    require(installation.level(0).walls()[door].type==I::WallType::None,"Right-click did not clear Door");
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,false); frame();
    installation.setDoor(0,door,true);
    io.AddKeyEvent(ImGuiMod_Shift,true); frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true); event=frame();
    require(event.phase==DeckPointerPhase::None && !event.toggleDoor,"Shift-click authored Door before Play");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false); frame();
    running=true; frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true); event=frame();
    require(event.phase==DeckPointerPhase::Press && event.toggleDoor && event.wall==door,
        "Running Shift-click did not toggle hovered Door");
    open={door}; event=frame();
    require(!event.toggleDoor,"Holding Shift-click repeatedly toggled Door");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false); frame();
    // The surrounding Wall remains solid; Shift-clicking its jamb is not a Door hit.
    io.AddMousePosEvent(origin.x+2.1f*34,origin.y+2*34); frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);event=frame();
    require(event.phase==DeckPointerPhase::None && !event.toggleDoor,"Door jamb counted as clear-opening hit");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false); frame();
    // Nearest is not the same as hovered: a click away from the line refuses.
    io.AddMousePosEvent(origin.x+2.5f*34,origin.y+2.4f*34); frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true); event=frame();
    require(event.phase==DeckPointerPhase::None && !event.toggleDoor,"Off-Door Shift-click toggled nearest edge");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false); frame();
    io.AddMousePosEvent(origin.x+0.5f*34,origin.y+0.5f*34); frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true); event=frame();
    require(!event.toggleDoor && event.phase==DeckPointerPhase::None,"Shift-click on empty space operated Door");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false); frame();
    io.AddMousePosEvent(origin.x+2.5f*34,origin.y+2*34); frame();
    mode=EditMode::Wall;
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true); event=frame();
    require(!event.toggleDoor,"Door toggle escaped Door mode");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false); frame();
    mode=EditMode::Door;
    io.AddKeyEvent(ImGuiMod_Shift,false); frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true); event=frame();
    require(!event.toggleDoor,"Unmodified click toggled running Door");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false); frame();
    ImGui::DestroyContext();
}
void gridRendering() {
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1024,768}; io.DeltaTime=1/60.0f;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    I installation(1,1);
    InstallationRenderSettings settings;
    // Default is byte-for-byte the old floor colour. Thermal selection must
    // outline rather than wash the exact reference-green floor with yellow.
    for (const auto mode:{InstallationRenderMode::Default,InstallationRenderMode::Thermal,InstallationRenderMode::Pressure}) {
        settings.mode=mode;
        const bool thermal=mode==InstallationRenderMode::Thermal;
        const bool heatmap=mode!=InstallationRenderMode::Default;
        for (int frame=0;frame<2;++frame) {
            ImGui::NewFrame(); ImGui::SetNextWindowSize({700,600});
            ImGui::Begin("Installation rendering");
            drawInstallationRenderModeCombo(settings);
            const auto cursor=ImGui::GetCursorPosY();
            drawThermalRenderControls(settings);
            require((ImGui::GetCursorPosY()>cursor)==thermal,"Thermal controls visible in wrong render mode");
            require(settings.thermal.valid(),"Slider bounds corrupted colour scale");
            const auto pressureCursor=ImGui::GetCursorPosY();
            drawPressureRenderControls(settings);
            require((ImGui::GetCursorPosY()>pressureCursor)==(mode==InstallationRenderMode::Pressure),"Pressure controls visible in wrong mode");
            require(settings.pressure.valid(),"Pressure slider bounds corrupted scale");
            auto* draw=ImGui::GetWindowDrawList(); const auto first=draw->VtxBuffer.Size;
            const std::vector<SectorColour> colours{{0,1,0}};
            drawDeckGrid("Grid",installation.level(0),EditMode::Sector,0,
                heatmap ? std::span<const SectorColour>(colours) : std::span<const SectorColour>{});
            const auto expected=heatmap ? IM_COL32(0,255,0,255) : IM_COL32(126,168,200,255);
            require(draw->VtxBuffer.Size>=first+4,"Grid did not draw floor");
            for (int n=0;n<4;++n) require(draw->VtxBuffer[first+n].col==expected,"Grid did not use Sector temperature colour");
            ImGui::End(); ImGui::Render();
        }
    }
    ImGui::DestroyContext();
}
}
int main() {
    try { scaleMapping(); pressureRendering(); runtimeColours(); doorInput(); gridRendering(); }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
