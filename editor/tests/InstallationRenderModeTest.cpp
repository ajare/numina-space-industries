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
void gridRendering() {
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1024,768}; io.DeltaTime=1/60.0f;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    I installation(1,1);
    InstallationRenderSettings settings;
    // Default is byte-for-byte the old floor colour. Thermal selection must
    // outline rather than wash the exact reference-green floor with yellow.
    for (const bool thermal:{false,true}) {
        settings.mode=thermal ? InstallationRenderMode::Thermal : InstallationRenderMode::Default;
        for (int frame=0;frame<2;++frame) {
            ImGui::NewFrame(); ImGui::SetNextWindowSize({700,600});
            ImGui::Begin("Installation rendering");
            drawInstallationRenderModeCombo(settings);
            const auto cursor=ImGui::GetCursorPosY();
            drawThermalRenderControls(settings);
            require((ImGui::GetCursorPosY()>cursor)==thermal,"Thermal controls visible in wrong render mode");
            require(settings.thermal.valid(),"Slider bounds corrupted colour scale");
            auto* draw=ImGui::GetWindowDrawList(); const auto first=draw->VtxBuffer.Size;
            const std::vector<SectorColour> colours{{0,1,0}};
            drawDeckGrid("Grid",installation.level(0),EditMode::Sector,0,
                thermal ? std::span<const SectorColour>(colours) : std::span<const SectorColour>{});
            const auto expected=thermal ? IM_COL32(0,255,0,255) : IM_COL32(126,168,200,255);
            require(draw->VtxBuffer.Size>=first+4,"Grid did not draw floor");
            for (int n=0;n<4;++n) require(draw->VtxBuffer[first+n].col==expected,"Grid did not use Sector temperature colour");
            ImGui::End(); ImGui::Render();
        }
    }
    ImGui::DestroyContext();
}
}
int main() {
    try { scaleMapping(); runtimeColours(); gridRendering(); }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
