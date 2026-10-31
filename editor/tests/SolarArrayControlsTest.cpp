#include "SolarArrayControls.h"
#include "SimulationPlayback.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <vector>
#include "station/editor/EditorSession.h"
#include "imgui.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace station::editor;
namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
// The Orbital period is derived now, from the world's mass and the height
// above its surface. These fixtures want a fast clock so an eclipse cycle
// arrives inside a handful of frames, so rather than typing a period they
// can no longer type they author the mass that makes the requested period
// true at the fixture's own radius: M = 4 pi^2 r^3 / (G T^2). The ride
// comes out exactly as the fixture wanted it and the record says honestly
// what world it would take.
void setPeriod(nsi::core::Installation::Orbit& orbit, double periodS) {
    const double r = orbit.orbitalRadiusM();
    orbit.planetMassKg = 4.0 * nsi::core::kPi * nsi::core::kPi * r * r * r
                      / (nsi::core::GravitationalConstantM3PerKgS2 * periodS * periodS);
}
void check() {
    EditorSession session;
    session.setSolarArray(Installation::SolarArray{60});
    const auto original = session.installation().serialize();
    require(original.find("solarArrayEnabled") == std::string::npos,
        "the removed enable field is still written to the wire");

    // A document saved while the array carried its own switch still loads.
    // The leftover field is ignored rather than refused - an authored array
    // is live whatever it said - and the size and technology the author
    // chose carry over untouched.
    Installation legacy;
    legacy.deserialize("solarArrayEnabled: false\n" + original);
    require(legacy == session.installation(),
        "a leftover enable field changed the document it cannot honour");
    require(legacy.serialize() == original,
        "a leftover enable field survived the round trip");

    // Exercise the actual Editor file boundary, not just a settings copy.
    YamlDocumentFiles files;
    const auto path = std::filesystem::temp_directory_path()/"nsi-solar-controls-test.yaml";
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove(p); } } cleanup{path};
    files.write(path,session.installation());
    auto loaded=files.read(path);
    require(loaded==session.installation(), "authored array changed during save/reload");
    session.replaceDocument(std::move(loaded));
    require(!session.unsavedChanges(), "loaded design is dirty");
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
    setPeriod(orbit,1); session.setOrbit(orbit); render();
    playback.togglePlaying();
    for (int i=0;i<5;++i) playback.update(.1);
    require(render().find("Solar array — Eclipse")!=std::string::npos,"live playback did not enter eclipse");
    // At the geometric shadow boundary the centred penumbra transmits half
    // the direct beam. Offset the next tick's midpoint to that boundary.
    nsi::systems::StellarExposureEvaluator sky(orbit);
    orbit.phaseOffsetPercent=50-50*sky.eclipseDurationSeconds()/orbit.orbitalPeriodSeconds()-5;
    session.setOrbit(orbit);
    require(render().find("Solar array — 9.4 kW")!=std::string::npos,"penumbra generation not rendered");
    setPeriod(orbit,5570); orbit.phaseOffsetPercent=50;
    session.setOrbit(orbit);
    require(render().find("Solar array — Eclipse")!=std::string::npos,"eclipse shown as a measured zero");
    session.setOrbit(std::nullopt);
    require(render().find("Solar array — No star")!=std::string::npos,"absent Orbit shown as eclipse");
    orbit.phaseOffsetPercent=0; session.setOrbit(orbit);
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
// #73: the Cell technology combo shows the systems catalogue's display
// names and nothing else. The Editor keeps no private copy of the wording,
// so the silicon entry reads "Space-grade silicon" the way CONTEXT.md
// spells it and the stale "Space silicon" never reaches the screen.
void technologyLabels() {
    EditorSession session;
    SimulationPlayback playback;
    auto render = [&](Installation::SolarCellTechnology tech) {
        session.setSolarArray(Installation::SolarArray{60, tech});
        playback.synchronize(session.installation());
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({600,450});
        ImGui::Begin("Cell technology", nullptr, ImGuiWindowFlags_NoTitleBar);
        ImGui::LogToBuffer();
        drawSolarArrayPanel(session, playback.snapshotFor(session.installation()));
        std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish(); ImGui::End(); ImGui::Render();
        return text;
    };
    for (std::size_t i = 0; i < station::SolarCellTechnologyLabels.size(); ++i) {
        const auto text = render(static_cast<Installation::SolarCellTechnology>(i));
        require(text.find(station::SolarCellTechnologyLabels[i]) != std::string::npos,
            "cell technology combo did not render the catalogue display label");
    }
    require(render(Installation::SolarCellTechnology::SpaceSilicon).find("Space silicon")
        == std::string::npos, "stale silicon wording reached the Editor");
}
// The generation graph rests on two things the renderer cannot show on its
// own: what SimulationPlayback records for each header state, and how a
// history of those samples resolves into shaded Eclipse stretches. Both are
// asserted here at the data level, so the picture below is checked against
// the numbers it is drawn from rather than the pixels it makes.
void generationSamples() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EditorSession session;
    session.setSolarArray(Installation::SolarArray{60});
    const double fullSun = 60 * 0.85 * 0.32 * 0.85 * 1361.0 / 1000.0;

    SimulationPlayback playback;
    playback.reset(session.installation());
    require(playback.solarHistory().size() == 1, "reset did not seed one generation sample");
    const auto& seed = playback.solarHistory().front();
    require(seed.seconds == 0, "generation seed is not at t=0");
    require(std::abs(seed.availableKW - fullSun) < 1e-8, "lit array seeded with the wrong generation");
    require(!seed.eclipse, "lit array seeded as eclipsed");

    playback.togglePlaying();
    for (int i = 0; i < 20; ++i)
        playback.update(.1);
    require(playback.solarHistory().size() == 21,
        "generation is not sampled once per committed tick");
    for (const auto& sample : playback.solarHistory())
        require(std::isfinite(sample.availableKW), "a lit run recorded a gap");

    // The window is sixty seconds of simulation time, not sixty seconds of
    // wall time and not the whole run: at ten times speed a minute of sim
    // time passes in six wall seconds, and everything older is dropped.
    SimulationPlayback longRun;
    longRun.reset(session.installation());
    longRun.setSpeedIndex(4);
    longRun.togglePlaying();
    for (int i = 0; i < 90; ++i)
        longRun.update(.1);
    const auto window = longRun.solarHistory();
    require(!window.empty(), "a long run lost its generation history");
    require(window.back().seconds > 60.0, "the long run never passed the window length");
    require(window.back().seconds - window.front().seconds <= 60.0 + 1e-9,
        "generation history outgrew the last minute");

    // A disabled array and a starless one are the states the header spells
    // in words. The history agrees, and leaves them out rather than
    // recording a zero that would read as a panel generating nothing under
    // a live sky. Zero blanket area is how an author disables an array now
    // that the enable switch is gone, and the systems side reports it as
    // such.
    session.setSolarArray(Installation::SolarArray{0});
    SimulationPlayback disabledArray;
    disabledArray.reset(session.installation());
    require(std::isnan(disabledArray.solarHistory().front().availableKW),
        "a disabled array recorded a measured zero");
    session.setSolarArray(Installation::SolarArray{60});

    session.setOrbit(std::nullopt);
    SimulationPlayback starless;
    starless.reset(session.installation());
    require(std::isnan(starless.solarHistory().front().availableKW),
        "an array with no Orbit recorded a measured zero");
    // The systems side reads its shadow state off the light term alone, so a
    // starless array arrives flagged as shadowed. That is not a contradiction
    // and not something the graph has to unpick: the sample is a gap, and a
    // gap shades nothing whatever its flag says. The header keeps "No star"
    // ahead of "Eclipse" for the same reason (#69).
}

void eclipseBands() {
    using Sample = SolarGenerationSample;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    require(solarEclipseBands({}).empty(), "an empty history produced a band");

    const std::vector<Sample> lit{{0, 10, false}, {1, 10, false}, {2, 10, false}};
    require(solarEclipseBands(lit).empty(), "a sunlit history produced a band");

    const std::vector<Sample> one{{0, 10, false}, {1, 0, true}, {2, 0, true}, {3, 10, false}};
    const auto contiguous = solarEclipseBands(one);
    require(contiguous.size() == 1 && contiguous[0].first == 1 && contiguous[0].second == 2,
        "a contiguous eclipse did not form one band");

    // A gap closes the band even when it carries the shadow flag. "Switched
    // off" and "no star" are not night, and a shadow may not be carried
    // across a stretch where nothing was read.
    const std::vector<Sample> split{{0, 0, true}, {1, 0, true}, {2, nan, true},
                                   {3, 0, true}, {4, 0, true}};
    const auto halves = solarEclipseBands(split);
    require(halves.size() == 2 && halves[0].second == 1 && halves[1].first == 3,
        "a gap did not split the eclipse into two bands");

    // A lone eclipsed observation has no width to shade.
    const std::vector<Sample> lone{{0, 10, false}, {1, 0, true}, {2, 10, false}};
    require(solarEclipseBands(lone).empty(), "a lone eclipse sample was banded at zero width");

    const std::vector<Sample> trailing{{0, 0, true}, {1, 0, true}};
    const auto open = solarEclipseBands(trailing);
    require(open.size() == 1 && open[0].second == 1, "a trailing eclipse was not banded");
}

void generationGraph() {
    EditorSession session;
    SimulationPlayback playback;
    session.setSolarArray(Installation::SolarArray{60});
    ImPlotPlot* plot = nullptr;
    auto render = [&](std::span<const SolarGenerationSample> history, bool inspect = false) {
        playback.synchronize(session.installation());
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({640, 640});
        ImGui::Begin("Solar generation graph", nullptr, ImGuiWindowFlags_NoTitleBar);
        ImGui::LogToBuffer();
        drawSolarArrayPanel(session, playback.snapshotFor(session.installation()), history);
        // GetPlot resolves the id through the current window, so it can only
        // be asked while the panel's own window is open. The pointer it hands
        // back is owned by the ImPlot context and stays readable afterwords.
        plot = inspect ? ImPlot::GetPlot("Solar generation — last minute###SolarGeneration")
                       : nullptr;
        const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::End();
        ImGui::Render();
        return text;
    };

    const auto empty = std::span<const SolarGenerationSample>{};
    auto text = render(empty, true);
    require(text.find("Generation history unavailable.") != std::string::npos,
        "an empty history did not say so");
    require(plot == nullptr, "an empty history opened a plot anyway");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<SolarGenerationSample> gaps{{0, nan, false}, {0.1, nan, false}};
    text = render(gaps, true);
    require(text.find("No generation to plot") != std::string::npos,
        "an all-gap history did not say so");
    require(plot == nullptr, "an all-gap history opened a plot anyway");

    playback.togglePlaying();
    for (int i = 0; i < 20; ++i)
        playback.update(.1);
    render(playback.solarHistory(), true);
    require(plot != nullptr, "the generation plot is missing after playback");
    require(std::string(plot->GetAxisLabel(plot->Axes[ImAxis_X1])) == "Simulation time (s)",
        "the generation plot's x axis is not simulation time");
    require(std::string(plot->GetAxisLabel(plot->Axes[ImAxis_Y1])) == "Generation (kW)",
        "the generation plot's y axis is not kilowatts");
    const double now = playback.solarHistory().back().seconds;
    require(std::abs(plot->Axes[ImAxis_X1].Range.Min - std::max(0.0, now - 60.0)) < 1e-9
            && std::abs(plot->Axes[ImAxis_X1].Range.Max - std::max(60.0, now)) < 1e-9,
        "the generation window is not pinned to the last minute");
    const double fullSun = 60 * 0.85 * 0.32 * 0.85 * 1361.0 / 1000.0;
    const auto& y = plot->Axes[ImAxis_Y1].Range;
    require(y.Min <= fullSun + 1e-9 && y.Max >= fullSun - 1e-9,
        "the generation axis omits the lit output");
    require(ImGui::GetDrawData()->Valid && ImGui::GetDrawData()->TotalVtxCount > 0,
        "the generation graph did not render");
}

// A whole orbit of history must show its shadow: the band is the only place
// the graph can say why the curve is flat.
void eclipseGraph() {
    EditorSession session;
    session.setSolarArray(Installation::SolarArray{60});
    auto orbit = *session.installation().orbit();
    setPeriod(orbit, 10);
    session.setOrbit(orbit);
    SimulationPlayback playback;
    playback.reset(session.installation());
    playback.togglePlaying();
    for (int i = 0; i < 110; ++i)
        playback.update(.1);
    const auto trace = playback.solarHistory();
    require(std::any_of(trace.begin(), trace.end(), [](const auto& s) { return s.eclipse; }),
        "a full orbit recorded no eclipse sample");
    const auto bands = solarEclipseBands(trace);
    require(!bands.empty(), "eclipse samples produced no band to shade");
    for (const auto& band : bands) {
        require(band.second > band.first, "an eclipse band has no width");
        require(band.first >= 0 && band.second <= trace.back().seconds + 1e-9,
            "an eclipse band lies outside the recorded window");
    }

    // The shadow has to reach the draw list, not just the geometry that
    // would produce it: a band computed and never painted is the same as no
    // band at all.
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({640, 640});
    ImGui::Begin("Eclipse graph", nullptr, ImGuiWindowFlags_NoTitleBar);
    drawSolarArrayPanel(session, playback.snapshotFor(session.installation()), trace);
    ImGui::End();
    ImGui::Render();
    std::size_t shaded = 0;
    auto* drawData = ImGui::GetDrawData();
    for (int list = 0; list < drawData->CmdListsCount; ++list)
        for (int vertex = 0; vertex < drawData->CmdLists[list]->VtxBuffer.Size; ++vertex)
            if (drawData->CmdLists[list]->VtxBuffer[vertex].col == IM_COL32(38, 58, 92, 120))
                ++shaded;
    require(shaded >= 4 * bands.size(), "the eclipse band was not painted behind the curve");
}
}
int main() {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    // The shared frame setup every renderer below draws into. It lives here
    // rather than in one test so the ones that count painted vertices see a
    // screen the same size the app would give them.
    auto& io = ImGui::GetIO();
    io.DisplaySize = {640, 480}; io.DeltaTime = 1.0f/60;
    io.IniFilename = nullptr;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    try {
        check();
        header();
        technologyLabels();
        generationSamples();
        eclipseBands();
        generationGraph();
        eclipseGraph();
    } catch (const std::exception& e) {
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        std::cerr << e.what() << '\n';
        return 1;
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}
