#include "InstallationRenderMode.h"
#include "InstallationRenderControls.h"
#include "GasInjectorControls.h"
#include "DeckGridWidget.h"
#include "SimulationPlayback.h"
#include <station/StationSystemsRuntime.h>
#include "station/editor/EditorSession.h"
#include <nsi/systems/Atmosphere.h>
#include "imgui.h"
#include "imgui_internal.h"
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
void componentRendering() {
    constexpr std::array modes{InstallationRenderMode::Oxygen,InstallationRenderMode::Nitrogen,
        InstallationRenderMode::CarbonDioxide,InstallationRenderMode::WaterVapour};
    const std::array<double,4> masses{1,1,1,1};
    const std::array<double,4> molar{station::StationMolarMassOxygenKgPerMol,station::StationMolarMassNitrogenKgPerMol,
        station::StationMolarMassCarbonDioxideKgPerMol,station::StationMolarMassWaterKgPerMol};
    double totalMoles=0;for (const auto m:molar) totalMoles+=1/m;
    require(!atmosphereRenderComponent(InstallationRenderMode::Thermal),"Thermal mode mapped to a gas");
    I scene(std::vector<I::Level>{I::Level(2,1),I::Level(1,1)});
    const auto authored=scene.serialize();SimulationPlayback playback;playback.reset(scene);
    auto snapshot=playback.runtime()->snapshot();const auto committed=snapshot;
    for (auto& cell:snapshot.cellAtmospheres) {cell.massKg=masses;cell.resolvedGas=true;}
    // Distinct Cells in the same Sector must not use its aggregate inventory.
    snapshot.cellAtmospheres[1].massKg={2,2,2,2};
    std::reverse(snapshot.cellAtmospheres.begin(),snapshot.cellAtmospheres.end());
    InstallationRenderSettings settings;
    ComponentColourScale scale{0,1,2};
    colour(componentColour(0,scale),{0,0,1});colour(componentColour(1,scale),{0,1,0});
    colour(componentColour(2,scale),{1,0,0});colour(componentColour(3,scale),{1,0,0});
    colour(componentColour(.5,scale),{0,.5f,.5f});
    require(!componentColour(-1,scale),"Negative mass received a colour");
    scale.reference=scale.minimum;require(!scale.valid() && !componentColour(1,scale),"Degenerate component scale accepted");
    for (std::size_t gas=0;gas<modes.size();++gas) {
        settings.mode=modes[gas];require(atmosphereRenderComponent(settings.mode)==gas,"Wrong constituent mapping");
        require(settings.atmosphere.mass[gas].valid() && settings.atmosphere.percentage[gas].valid(),"Invalid default component scale");
        const double percent=100*(1/molar[gas])/totalMoles;
        near(static_cast<float>(*atmosphereComponentValue(masses,gas,AtmosphereRenderQuantity::Percentage,true)),static_cast<float>(percent));
        require(std::abs(percent-25)>.01,"Composition accidentally used mass percentage");
        settings.atmosphere.quantity=AtmosphereRenderQuantity::Mass;settings.atmosphere.mass[gas]={0,1,2};
        const auto colours=cellRenderColours(scene,0,settings,&snapshot);
        require(colours[0]==SectorColour{0,1,0} && colours[1]==SectorColour{1,0,0},"Component mass was not local to each Cell");
        require(cellRenderColours(scene,1,settings,&snapshot)[0]==SectorColour{0,1,0},"Component tint used wrong Level");
        settings.atmosphere.quantity=AtmosphereRenderQuantity::Percentage;
        const auto percentages=cellRenderColours(scene,0,settings,&snapshot);
        require(percentages[0]==percentages[1] && percentages[0]==*componentColour(percent,settings.atmosphere.scale(gas)),"Composition changed when total inventory doubled");
        auto invalid=snapshot;for (auto& c:invalid.cellAtmospheres) {c.resolvedGas=false;c.massKg={};}
        require(cellRenderColours(scene,0,settings,&invalid)[0]==unresolvedGasColour(),"Empty gas fabricated a percentage");
        settings.atmosphere.quantity=AtmosphereRenderQuantity::Mass;
        require(cellRenderColours(scene,0,settings,&invalid)[0]==SectorColour{0,0,1},"Zero constituent mass was not measurable");
        for (auto& c:invalid.cellAtmospheres) c.massKg=masses;
        require(cellRenderColours(scene,0,settings,&invalid)[0]==SectorColour{0,1,0},"Unresolved trace gas hid retained mass");
        invalid=snapshot;for (auto& c:invalid.cellAtmospheres) ++c.sectorUid;
        const auto defaults=std::vector<SectorColour>(2,defaultSectorColour(I::SectorType::Interior));
        require(cellRenderColours(scene,0,settings,&invalid)==defaults,"Stale component telemetry coloured Cell");
        invalid=snapshot;invalid.enabled=false;
        require(cellRenderColours(scene,0,settings,&invalid)==defaults && cellRenderColours(scene,0,settings)==defaults,"Missing/disabled component telemetry coloured Cell");
        settings.atmosphere.quantity=AtmosphereRenderQuantity::Percentage;
        auto bad=masses;bad[gas]=std::numeric_limits<double>::quiet_NaN();
        require(!atmosphereComponentValue(bad,gas,settings.atmosphere.quantity,true),"Nonfinite composition accepted");
        bad=masses;bad[(gas+1)%4]=-1;
        require(!atmosphereComponentValue(bad,gas,settings.atmosphere.quantity,true),"Negative mixture inventory accepted");
        // Aggregate fallback uses total constituent inventories, not partial-pressure ratios.
        auto aggregate=committed;for (auto& a:aggregate.atmospheres) {
            a.oxygenKg=a.nitrogenKg=a.carbonDioxideKg=a.waterVapourKg=1;
            a.oxygenPartialPressureKPa=0;a.resolvedGas=true;
        }
        std::reverse(aggregate.atmospheres.begin(),aggregate.atmospheres.end());
        require(sectorRenderColours(scene,0,settings,&aggregate)[0]==*componentColour(percent,settings.atmosphere.scale(gas)),"Sector composition ignored inventories or stable identity");
        auto absent=scene;auto yaml=absent.serialize();yaml.replace(yaml.find("Earth-like"),10,"Void");absent.deserialize(yaml);
        require(cellRenderColours(absent,0,settings,&snapshot)==defaults,"Void preset acquired constituent telemetry");
    }
    require(!atmosphereComponentValue(masses,4,AtmosphereRenderQuantity::Mass,true),"Invalid species accepted");
    require(scene.serialize()==authored && playback.runtime()->snapshot()==committed,"Render settings changed authoring or physics");
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
    // The heatmap's cooling ramp is calibrated against a dark sky; a new
    // document now authors a live orbit, so ask for darkness explicitly.
    session.setOrbit(std::nullopt);
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
void injectorInput() {
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize={1024,768};io.DeltaTime=1/60.f;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    I scene(3,2);const auto authored=scene.serialize();GasInjectorSettings settings;
    require(settings.gas==0 && settings.kgPerS==.1f,"Unexpected injector defaults");
    EditMode mode=EditMode::Injector;ImVec2 origin;std::string text;
    auto frame=[&]() {
        ImGui::NewFrame();ImGui::SetNextWindowPos({40,40});ImGui::SetNextWindowSize({700,600});
        ImGui::Begin("Injector input");origin=ImGui::GetCursorScreenPos();
        const auto event=drawDeckGrid("InjectorGrid",scene.level(0),mode);
        ImGui::LogToBuffer();if (mode==EditMode::Injector) drawGasInjectorControls(settings);
        text=ImGui::GetCurrentContext()->LogBuffer.c_str();ImGui::LogFinish();
        ImGui::End();ImGui::Render();return event;
    };
    frame();frame();
    require(text.find("Gas injector")!=std::string::npos && text.find("Rate (kg/s)")!=std::string::npos && text.find("Oxygen")!=std::string::npos,"Injector header/widgets missing");
    io.AddMousePosEvent(origin.x+17,origin.y+17);frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);auto event=frame();
    require(event.injectHeld && event.cell && event.cell->x==0 && event.phase==DeckPointerPhase::Press,"Injector did not acquire grid left-button hold");
    event=frame();require(event.injectHeld && event.phase==DeckPointerPhase::Drag,"Stationary held mouse did not keep injecting");
    io.AddMousePosEvent(origin.x+51,origin.y+17);event=frame();
    require(event.injectHeld && event.cell && event.cell->x==1,"Injector did not follow dragged Cell");
    io.AddMousePosEvent(origin.x-20,origin.y-20);event=frame();require(!event.injectHeld,"Off-grid hold kept injecting");
    io.AddMousePosEvent(origin.x+17,origin.y+17);event=frame();require(event.injectHeld,"Returning held gesture did not resume injector");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);event=frame();require(!event.injectHeld,"Released mouse kept injecting");
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,true);event=frame();require(!event.injectHeld,"Right mouse triggered injector");
    io.AddMouseButtonEvent(ImGuiMouseButton_Right,false);frame();
    mode=EditMode::Cell;io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame();
    mode=EditMode::Injector;event=frame();require(!event.injectHeld,"Switching modes inherited an authoring stroke");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);event=frame();require(event.injectHeld,"Fresh Injector gesture refused");
    mode=EditMode::Sector;event=frame();require(!event.injectHeld,"Switching away from Injector retained source");
    mode=EditMode::Injector;event=frame();require(!event.injectHeld,"Switching back revived old injector gesture");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);event=frame();require(event.injectHeld,"Focus test setup failed");
    io.AddFocusEvent(false);event=frame();require(!event.injectHeld,"Focus loss retained injection");
    io.AddFocusEvent(true);frame();
    const std::array<const char*,4> names{"Oxygen","Nitrogen","Carbon dioxide","Water vapour"};
    for (int gas=0;gas<4;++gas) {settings.gas=gas;frame();require(text.find(names[gas])!=std::string::npos,"Injector omitted selected species");}
    require(scene.serialize()==authored,"Injector pointer gesture modified authoring");
    ImGui::DestroyContext();
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
void evacuatedThermalRendering() {
    // LocalStructure decompression: render resolved gas from its own temperature,
    // and label unresolved gas explicitly rather than as a cold surface.
    I scene(3,4);scene.setHullType(I::HullType::MultiLayerBlanket);
    const auto door=scene.level(0).verticalWall(3,1);scene.setDoor(0,door,true);
    CellPlaybackRuntime runtime(scene);runtime.setDoorOpen(0,door,true);
    InstallationRenderSettings settings;settings.mode=InstallationRenderMode::Thermal;
    bool cooled=false,unresolved=false;
    for (int step=0;step<400;++step) {
        require(runtime.update(0.1),"Thermal decompression replay failed");const auto& snapshot=runtime.snapshot();
        const auto colours=cellRenderColours(scene,0,settings,&snapshot);
        for (const auto& c:snapshot.cellAtmospheres) {
            if (c.resolvedGas) {
                cooled=cooled || c.temperatureC<0;
                require(colours[c.gridIndex]==*thermalColour(c.temperatureC,settings.thermal),"Gas colour did not match local gas temperature");
            } else {
                unresolved=true;const auto colour=colours[c.gridIndex];
                require(colour.red==colour.green && colour.green==colour.blue,
                    "Evacuated room looked cold instead of explicitly temperature-unavailable");
            }
        }
    }
    require(cooled && unresolved,"Thermal replay did not cover cooling/unresolved phases");
    settings.mode=InstallationRenderMode::Default;
    for (const auto& colour:cellRenderColours(scene,0,settings,&runtime.snapshot()))
        require(colour==defaultSectorColour(I::SectorType::Interior),"Unresolved marker changed Default rendering");
    settings.mode=InstallationRenderMode::Pressure;
    for (const auto& colour:cellRenderColours(scene,0,settings,&runtime.snapshot()))
        require(colour==SectorColour{0,0,1},"Unresolved marker replaced valid near-zero pressure");
}
void cellRendering() {
    I scene(2,1);SimulationPlayback playback;playback.reset(scene);auto snapshot=playback.runtime()->snapshot();
    InstallationRenderSettings settings;settings.mode=InstallationRenderMode::Thermal;
    snapshot.cellAtmospheres[0].temperatureC=settings.thermal.minimumC;snapshot.cellAtmospheres[1].temperatureC=settings.thermal.maximumC;
    snapshot.cellAtmospheres[0].pressureKPa=0;snapshot.cellAtmospheres[1].pressureKPa=settings.pressure.maximumKPa;
    auto colours=cellRenderColours(scene,0,settings,&snapshot);
    require(colours.size()==2 && colours[0]==SectorColour{0,0,1} && colours[1]==SectorColour{1,0,0},"Cells in one Sector did not get independent temperatures");
    std::reverse(snapshot.cellAtmospheres.begin(),snapshot.cellAtmospheres.end());
    require(cellRenderColours(scene,0,settings,&snapshot)==colours,"Cell colours depended on snapshot order");
    auto invalid=snapshot;for (auto& c:invalid.cellAtmospheres) if (c.gridIndex==0) c.sectorUid++;
    require(cellRenderColours(scene,0,settings,&invalid)[0]==defaultSectorColour(I::SectorType::Interior),"Stale Cell uid coloured geometry");
    invalid=snapshot;invalid.atmospheres[0].resolvedGas=false;
    require(sectorRenderColours(scene,0,settings,&invalid)[0]==unresolvedGasColour(),"Aggregate view did not distinguish unresolved gas");
    for (auto& c:invalid.cellAtmospheres) if (c.gridIndex==0) c.resolvedGas=false;
    require(cellRenderColours(scene,0,settings,&invalid)[0]==unresolvedGasColour(),"Unresolved gas fabricated temperature colour");
    settings.mode=InstallationRenderMode::Pressure;
    require(cellRenderColours(scene,0,settings,&invalid)[0]==SectorColour{0,0,1},"Vacuum pressure lost its colour");
    auto yaml=scene.serialize();yaml.replace(yaml.find("atmosphere: Earth-like"),std::string("atmosphere: Earth-like").size(),"atmosphere: Void");
    I absent;absent.deserialize(yaml);
    const auto fallback=defaultSectorColour(I::SectorType::Interior);
    require(cellRenderColours(absent,0,settings,&snapshot)==std::vector<SectorColour>{fallback,fallback},"Void preset reused Cell atmosphere");
    I levels(std::vector<I::Level>{I::Level(1,1),I::Level(1,1)});SimulationPlayback multiple;multiple.reset(levels);
    auto layered=multiple.runtime()->snapshot();layered.cellAtmospheres[0].temperatureC=settings.thermal.minimumC;
    layered.cellAtmospheres[1].temperatureC=settings.thermal.maximumC;settings.mode=InstallationRenderMode::Thermal;
    require(cellRenderColours(levels,0,settings,&layered)[0]==SectorColour{0,0,1}
        && cellRenderColours(levels,1,settings,&layered)[0]==SectorColour{1,0,0},"Cell temperatures crossed Level identities");
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={900,700};io.DeltaTime=1/60.0f;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    for (auto& c:snapshot.cellAtmospheres) {c.velocityX=10;c.velocityY=0;c.windSpeedMS=10;c.forceN={10,0};}
    near(airflowLoadArrowFraction(0,10),0);near(airflowLoadArrowFraction(10,10),0.5);
    require(airflowLoadArrowFraction(1,10)<airflowLoadArrowFraction(10,10)
        && airflowLoadArrowFraction(0.001,10)<airflowLoadArrowFraction(0.01,10),"Force arrows have a fixed minimum length");
    require(airflowLoadArrowFraction(100,10)<airflowLoadArrowFraction(1000,10),"Force arrows saturate at a fixed threshold");
    require(airflowLoadArrowFraction(1,0)==0 && airflowLoadArrowFraction(std::numeric_limits<double>::quiet_NaN(),10)==0,"Invalid load scale accepted");
    int without=0,with=0;std::string text,loadedText;
    for (int frame=0;frame<6;++frame) {
        for (auto& c:snapshot.cellAtmospheres) {
            c.forceN={frame==3 ? 1e-9 : 10,0};c.forcesEnabled=frame!=4;c.windEnabled=frame!=5;
        }
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({700,600});ImGui::Begin("Cell observations");
        const auto origin=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();const auto first=draw->VtxBuffer.Size;
        ImGui::BeginDisabled();ImGui::LogToBuffer();
        drawDeckGrid("Native",scene.level(0),EditMode::Cell,std::nullopt,{},false,{},colours,snapshot.cellAtmospheres,0,frame>=2);
        text=ImGui::GetCurrentContext()->LogBuffer.c_str();ImGui::LogFinish();ImGui::EndDisabled();
        for (int v=0;v<4;++v) require(draw->VtxBuffer[first+v].col==IM_COL32(0,0,255,255),"Widget did not use Cell colour");
        for (int v=4;v<8;++v) require(draw->VtxBuffer[first+v].col==IM_COL32(255,0,0,255),"Widget used uniform Sector colour");
        if (frame==1) without=draw->VtxBuffer.Size-first;if (frame==2) {with=draw->VtxBuffer.Size-first;loadedText=text;}
        int arrows=0;for (int v=first;v<draw->VtxBuffer.Size;++v) arrows+=draw->VtxBuffer[v].col==IM_COL32(255,245,160,240);
        if (frame==2 || frame==5) require(arrows>0,"Force arrows incorrectly depend on wind diagnostics");
        if (frame==3 || frame==4) require(arrows==0,"Tiny/disabled forces still displayed full wind arrows");
        if (frame==4) require(text.find("Object forces: disabled")!=std::string::npos,"Disabled forces were displayed as measurements");
        if (frame==5) require(text.find("Wind diagnostics: disabled")!=std::string::npos,"Disabled wind was displayed as a measurement");
        ImGui::End();ImGui::Render();io.AddMousePosEvent(origin.x+15,origin.y+15);
    }
    require(with>without,"Airflow load arrows did not render");
    require(loadedText.find("Pressure")!=std::string::npos && loadedText.find("Wind (")!=std::string::npos && loadedText.find("Wet mole")!=std::string::npos && loadedText.find("Reference load")!=std::string::npos,
        "Read-only playing grid did not expose Cell observations");
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
    for (const auto mode:{InstallationRenderMode::Default,InstallationRenderMode::Thermal,InstallationRenderMode::Pressure,
        InstallationRenderMode::Structure,InstallationRenderMode::Oxygen,InstallationRenderMode::Nitrogen,
        InstallationRenderMode::CarbonDioxide,InstallationRenderMode::WaterVapour}) {
        settings.mode=mode;
        const bool thermal=mode==InstallationRenderMode::Thermal || mode==InstallationRenderMode::Structure;
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
            const auto component=atmosphereRenderComponent(mode);
            settings.atmosphere.quantity=frame ? AtmosphereRenderQuantity::Mass : AtmosphereRenderQuantity::Percentage;
            const auto componentCursor=ImGui::GetCursorPosY();
            ImGui::LogToBuffer();drawAtmosphereRenderControls(settings);
            const std::string text=ImGui::GetCurrentContext()->LogBuffer.c_str();ImGui::LogFinish();
            require((ImGui::GetCursorPosY()>componentCursor)==component.has_value(),"Component controls visible in wrong mode");
            if (component) {
                require(settings.atmosphere.scale(*component).valid(),"Component controls corrupted colour scale");
                require(text.find(frame ? "Minimum mass (kg)" : "Minimum percentage (%)")!=std::string::npos,"Selected component quantity controls missing");
                require(text.find("rendering")!=std::string::npos && text.find("Quantity")!=std::string::npos,"Component collapsible header/quantity absent");
            }
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
    try { evacuatedThermalRendering(); scaleMapping(); pressureRendering(); componentRendering(); runtimeColours(); injectorInput(); doorInput(); cellRendering(); gridRendering(); }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
