#include "SolarArrayControls.h"
#include "SimulationPlayback.h"
#include "imgui_internal.h"
#include <cmath>
#include <limits>
#include <optional>
#include "station/editor/EditorSession.h"
#include "imgui.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace station::editor;
namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void check() {
    EditorSession session;
    require(session.installation().solarArrayEnabled(), "new document solar is not enabled");
    const auto original = session.installation().serialize();
    require(original.find("solarArrayEnabled") == std::string::npos, "default adds wire churn");
    Installation legacy;
    legacy.setSolarArrayEnabled(false);
    legacy.deserialize(original);
    require(legacy.solarArrayEnabled(), "missing field did not restore enabled default");

    auto& io = ImGui::GetIO();
    io.DisplaySize = {640, 480}; io.DeltaTime = 1.0f/60;
    io.IniFilename = nullptr;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImVec2 point;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({400,200});
        ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoTitleBar);
        drawSolarArrayControls(session);
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        point = {(min.x+max.x)/2, (min.y+max.y)/2};
        ImGui::End(); ImGui::Render();
    };
    auto click = [&] {
        io.AddMousePosEvent(point.x, point.y); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMouseButtonEvent(0,false); frame();
    };
    frame(); frame(); click();
    require(!session.installation().solarArrayEnabled(), "checkbox did not write authored flag");
    require(session.unsavedChanges(), "checkbox did not dirty document");
    session.undo();
    require(session.installation().serialize()==original && !session.unsavedChanges(), "undo did not restore document");
    session.redo();
    require(!session.installation().solarArrayEnabled(), "redo did not restore disabled choice");

    // Exercise the actual Editor file boundary, not just a settings copy.
    YamlDocumentFiles files;
    const auto path = std::filesystem::temp_directory_path()/"nsi-solar-controls-test.yaml";
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove(p); } } cleanup{path};
    files.write(path,session.installation());
    auto loaded=files.read(path);
    require(loaded==session.installation(), "disabled design changed during save/reload");
    session.replaceDocument(std::move(loaded));
    require(!session.unsavedChanges(), "loaded design is dirty");
    session.setEditingEnabled(false);
    click();
    require(!session.installation().solarArrayEnabled() && !session.unsavedChanges(), "locked checkbox mutated document");
    session.setSolarArrayEnabled(true);
    require(!session.installation().solarArrayEnabled(), "session command bypassed playback lock");
    session.setEditingEnabled(true); click();
    require(session.installation().solarArrayEnabled() && session.unsavedChanges(), "reset did not unlock checkbox");
    files.write(path,session.installation());
    require(files.read(path)==session.installation(), "enabled design changed during save/reload");
}
void header() {
    EditorSession session;
    SimulationPlayback playback;
    auto& io=ImGui::GetIO();
    ImVec2 button;
    auto render=[&](std::optional<station::StationSystemsSnapshot> overrideSnapshot={}) {
        playback.synchronize(session.installation());
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({600,450});
        ImGui::Begin("Solar header",nullptr,ImGuiWindowFlags_NoTitleBar);
        ImGui::LogToBuffer();
        drawSolarArrayPanel(session,overrideSnapshot
            ? *overrideSnapshot : playback.snapshotFor(session.installation()));
        const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
        button={(min.x+max.x)/2,(min.y+max.y)/2};
        std::string text=ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish(); ImGui::End(); ImGui::Render();
        return text;
    };
    render();
    require(render().find("Solar array —")==std::string::npos,"absent array header visible");
    const double kFullSunKW=60*0.85*0.32*0.85*1361.0/1000.0;
    const auto original=session.installation().serialize();
    session.setSolarArray(Installation::SolarArray{60});
    require(session.unsavedChanges(),"array creation did not dirty document");
    session.undo(); require(session.installation().serialize()==original,"array undo changed legacy bytes");
    session.redo();
    require(render().find("Solar array — 18.9 kW")!=std::string::npos,"lit header not rendered");
    auto snapshot=playback.snapshotFor(session.installation());
    require(std::abs(snapshot->power.sources.front().availableKW-kFullSunKW)<1e-8,"wrong unclipped generation");
    require(snapshot->elapsedSeconds==0,"preview advanced the playback clock");
    playback.togglePlaying(); playback.update(.1);
    snapshot=playback.snapshotFor(session.installation());
    require(snapshot->healthy && std::abs(snapshot->power.sources.front().availableKW-kFullSunKW)<1e-8,
        "running output was clipped by consumer-less bus");
    require(std::abs(snapshot->elapsedSeconds-.1)<1e-8,"power clock changed native playback");
    auto orbit=*session.installation().orbit();
    orbit.orbitalPeriodS=1; session.setOrbit(orbit); render();
    playback.togglePlaying();
    for (int i=0;i<5;++i) playback.update(.1);
    require(render().find("Solar array — Eclipse")!=std::string::npos,"live playback did not enter eclipse");
    // At the geometric shadow boundary the centred penumbra transmits half
    // the direct beam. Offset the next tick's midpoint to that boundary.
    nsi::systems::StellarExposureEvaluator sky(orbit);
    orbit.phaseOffsetPercent=50-50*sky.eclipseDurationSeconds()/orbit.orbitalPeriodS-5;
    session.setOrbit(orbit);
    require(render().find("Solar array — 9.4 kW")!=std::string::npos,"penumbra generation not rendered");
    orbit.orbitalPeriodS=5570; orbit.phaseOffsetPercent=50;
    session.setOrbit(orbit);
    require(render().find("Solar array — Eclipse")!=std::string::npos,"eclipse shown as a measured zero");
    session.setOrbit(std::nullopt);
    require(render().find("Solar array — No star")!=std::string::npos,"absent Orbit shown as eclipse");
    session.setSolarArrayEnabled(false);
    require(render().find("Solar array — Disabled")!=std::string::npos,"disabled precedence lost");
    playback.togglePlaying(); playback.update(.1);
    snapshot=playback.snapshotFor(session.installation());
    require(snapshot->power.sources.front().disabled && snapshot->power.sources.front().availableKW==0,
        "disabled choice not mapped into running systems");
    session.setSolarArrayEnabled(true); orbit.phaseOffsetPercent=0; session.setOrbit(orbit);
    session.setSolarArray(Installation::SolarArray{60,Installation::SolarCellTechnology::SpaceSilicon});
    require(render().find("Solar array — 6.9 kW")!=std::string::npos,"technology not mapped");
    // #69: Eclipse is the systems-side shadow state, never a restatement of
    // zero output. Each snapshot below carries the field combination a real
    // runtime publishes for the case named in it; the header must keep the
    // cases apart.
    auto shows=[&](const station::StationSystemsSnapshot& snap,const char* want,const char* reject) {
        const auto text=render(snap);
        require(text.find(want)!=std::string::npos,("header did not render "+std::string(want)).c_str());
        require(reject==nullptr || text.find(reject)==std::string::npos, reject
            ? ("header rendered "+std::string(reject)+" for a snapshot that does not support it").c_str()
            : "header rendered a state the snapshot does not support");
    };
    auto state=[&](bool disabled,bool eclipse,double availableKW) {
        auto snap=playback.snapshotFor(session.installation());
        require(snap.has_value() && !snap->power.sources.empty(),"no preview snapshot to edit");
        auto& source=snap->power.sources.front();
        source.disabled=disabled; source.eclipse=eclipse; source.availableKW=availableKW;
        return *snap;
    };
    shows(state(false,true,0.0),"Solar array — Eclipse",nullptr);
    // A tracker stuck at availability 0 delivers nothing in full sun: the
    // panel is broken, it is not night.
    shows(state(false,false,0.0),"Solar array — <0.1 kW","Eclipse");
    // Penumbra is generating, just too little to print as a tenth.
    shows(state(false,false,0.000510490),"Solar array — <0.1 kW","0.0 kW");
    shows(state(false,false,0.049),"Solar array — <0.1 kW","0.0 kW");
    shows(state(false,false,0.05),"Solar array — 0.1 kW",nullptr);
    // A blanket worn to nothing is dead, not dark (#68), and Disabled wins
    // over every other reading, eclipse included.
    shows(state(true,false,0.0),"Solar array — Disabled","Eclipse");
    shows(state(true,true,0.0),"Solar array — Disabled",nullptr);
    // No star outranks the shadow too: an absent Orbit is semantic absence,
    // whatever the systems side happens to say about the light. The shadowed
    // snapshot is taken while the preview still matches the document.
    const auto shadowed=state(false,true,0.0);
    const auto orbitBeforeHeader=session.installation().orbit();
    session.setOrbit(std::nullopt);
    require(render(shadowed).find("Solar array — No star")!=std::string::npos,
        "absent Orbit did not outrank the published eclipse");
    session.setOrbit(orbitBeforeHeader);
    require(session.installation().orbit()==orbitBeforeHeader,"header test lost the authored orbit");
    Installation restored; restored.deserialize(session.installation().serialize());
    require(restored==session.installation(),"array round trip lost authored properties");
    auto noArray=restored; noArray.setSolarArray(std::nullopt);
    CellPlaybackRuntime nativeOnly(noArray),withPower(restored);
    require(nativeOnly.update(.1) && withPower.update(.1),"power/native coexistence update failed");
    require(nativeOnly.snapshot().atmospheres==withPower.snapshot().atmospheres
        && nativeOnly.snapshot().cellAtmospheres==withPower.snapshot().cellAtmospheres,
        "isolated power altered native atmosphere ownership");
    auto zero=restored; zero.setSolarArray(Installation::SolarArray{0});
    CellPlaybackRuntime zeroRuntime(zero);
    require(zeroRuntime.snapshot().power.sources.front().disabled,"zero area misreported as eclipse");
    session.setEditingEnabled(false); session.setSolarArray(std::nullopt);
    require(session.installation().solarArray().has_value(),"array command bypassed edit lock");
    session.setEditingEnabled(true); session.setSolarArray(std::nullopt);
    require(render().find("Solar array —")==std::string::npos,"removed header remains visible");
    for (double invalid : {-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected=false;
        try { restored.setSolarArray(Installation::SolarArray{invalid}); } catch (...) { rejected=true; }
        require(rejected,"invalid blanket area accepted");
    }
    auto click=[&] {
        io.AddMousePosEvent(button.x,button.y); render();
        io.AddMouseButtonEvent(0,true); render();
        io.AddMouseButtonEvent(0,false); render();
    };
    session.setEditingEnabled(false); click();
    require(!session.installation().solarArray(),"locked Add control mutated document");
    session.setEditingEnabled(true); click();
    require(session.installation().solarArray().has_value(),"Add control did not author an array");
}
}
int main() {
    ImGui::CreateContext();
    try { check(); header(); }
    catch (const std::exception& e) { ImGui::DestroyContext(); std::cerr<<e.what()<<'\n'; return 1; }
    ImGui::DestroyContext();
}
