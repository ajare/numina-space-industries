#include "OrbitSummary.h"
#include "SimulationPlayback.h"
#include "StellarExposureTrace.h"
#include "StructuralPanel.h"

#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"

#include "station/editor/EditorSession.h"
#include <nsi/core/ThermalPresets.h>
#include <nsi/systems/CellAtmosphere.h>
#include <nsi/systems/StellarExposure.h>
#include <station/Configuration.h>
#include <station/ThermalBody.h>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace station::editor;
using I = nsi::core::Installation;
using O = I::Orbit;
namespace core = nsi::core;

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kSecondsPerDay = 86400.0;

void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
bool near(double value, double expected, double tolerance) {
    return std::abs(value - expected) <= tolerance;
}
bool hasLine(const std::vector<std::string>& lines, std::string_view needle) {
    for (const auto& line : lines) if (line.find(needle) != std::string::npos) return true;
    return false;
}
bool bandCovers(const std::vector<std::pair<double, double>>& bands, double seconds, bool inclusiveEnd) {
    for (const auto& [from, to] : bands) {
        if (seconds >= from && (inclusiveEnd ? seconds <= to : seconds < to)) return true;
    }
    return false;
}
I sceneWith(const std::optional<O>& orbit) {
    I scene(2, 2);
    scene.setHullType(I::HullType::BareSkin);
    scene.setOrbit(orbit);
    return scene;
}

// Check the displayed values, including every converted second unit, rather
// than just the labels or the unformatted summary fields.
void summaryFormatsNumericValues() {
    const auto lines = orbitSummaryLines(orbitSummary(I(2, 2)));
    const std::array<std::string_view, 8> expected{
        "Stellar irradiance  1361.0 W/m^2",
        "Orbital period  5570.0 s (92.8 min)",
        "Orbital radius  6791000 m (6791 km)",
        "Beta angle  0.0000 rad (0.0 deg)",
        "Phase offset  0.0 s",
        "Albedo  0.300",
        "Eclipse duration  2158.2 s (36.0 min)",
        "Sunlit duration  3411.8 s (56.9 min)"};
    for (const auto text : expected) {
        bool found = false;
        for (const auto& line : lines) found = found || line == text;
        require(found, std::string("incorrect formatted reading: ").append(text).c_str());
    }

    // A nonzero, signed Beta angle catches a missing radians-to-degrees
    // conversion that the zero default cannot expose. This sky has no eclipse.
    const auto tilted = orbitSummaryLines(orbitSummary(
        sceneWith(O{1361.0, 6000.0, 42164000.0, 0.3, -0.5, 120.0})));
    require(hasLine(tilted, "Beta angle  -0.5000 rad (-28.6 deg)"),
            "nonzero Beta angle was not converted to degrees");
    require(hasLine(tilted, "Eclipse duration  0.0 s (0.0 min)"),
            "zero eclipse duration was not formatted correctly");
    require(hasLine(tilted, "Sunlit duration  6000.0 s (100.0 min)"),
            "fully sunlit duration was not converted to minutes");

    auto unavailable = orbitSummary(I(2, 2));
    unavailable.orbitalPeriodS = kNaN;
    unavailable.radiusFromPlanetCentreM = std::numeric_limits<double>::infinity();
    unavailable.betaAngleRad = -std::numeric_limits<double>::infinity();
    unavailable.eclipseDurationS = kNaN;
    const auto gaps = orbitSummaryLines(unavailable);
    require(hasLine(gaps, "Orbital period  unavailable")
                && hasLine(gaps, "Orbital radius  unavailable")
                && hasLine(gaps, "Beta angle  unavailable"),
            "nonfinite authored values were formatted as numbers");
    require(hasLine(gaps, "Derived values unavailable")
                && !hasLine(gaps, "Eclipse duration") && !hasLine(gaps, "Sunlit duration"),
            "unavailable durations were formatted as numbers");
}

// The authored numbers sit beside the derived ones, and every derived one
// is the evaluator's own rather than a second opinion.
void summaryShowsAuthoredBesideDerived() {
    const O orbit{1361.0, 5570.0, 6791000.0, 0.3, 0.25, 120.0};
    const auto scene = sceneWith(orbit);
    const auto summary = orbitSummary(scene);
    require(summary.authored && !summary.overridden, "an authored orbit was not reported as authored");
    require(summary.stellarIrradianceWPerM2 == 1361.0, "authored Stellar irradiance lost");
    require(summary.orbitalPeriodS == 5570.0, "authored Orbital period lost");
    require(summary.radiusFromPlanetCentreM == 6791000.0, "authored orbital radius lost");
    require(summary.betaAngleRad == 0.25, "authored Beta angle lost");
    require(summary.phaseOffsetS == 120.0, "authored phase offset lost");
    require(summary.albedo == 0.3, "authored Albedo lost");

    const nsi::systems::StellarExposureEvaluator evaluator(orbit);
    require(near(summary.eclipseDurationS, evaluator.eclipseFraction() * 5570.0, 1e-9),
            "eclipse duration is not the evaluator's own fraction over the Orbital period");
    require(near(summary.eclipseDurationS + summary.sunlitDurationS, 5570.0, 1e-9),
            "eclipse and sunlit durations do not sum to the Orbital period");
    require(summary.sunlitDurationS > summary.eclipseDurationS,
            "an ISS-like sky is not more sunlit than eclipsed");
    const double drift = kSecondsPerDay * 360.0
                      * (1.0 / 5570.0 - 1.0 / core::PlanetSiderealRotationS);
    require(near(summary.groundDriftDegPerDay, drift, 1e-6),
            "Ground drift is not the residue between the orbital and sidereal rates");
    require(summary.groundDriftDegPerDay > 0.0, "an under-synchronous orbit did not drift east");
    require(!summary.geostationary, "an ISS-like period claimed Geostationary");

    const auto lines = orbitSummaryLines(summary);
    const std::array<std::string_view, 9> labels{"Stellar irradiance", "Orbital period",
                                               "Orbital radius", "Beta angle", "Phase offset",
                                               "Albedo", "Eclipse duration", "Sunlit duration",
                                               "Ground drift"};
    for (const auto& label : labels)
        require(hasLine(lines, label), "summary is missing one of its readings");
    require(!hasLine(lines, "unavailable"), "a live sky reported an unavailable reading");
    require(hasLine(lines, station::NativeThermalModelRevision),
            "the summary is not labelled with the thermal model revision");
    require(hasLine(lines, core::NativeThermalPresetRevision),
            "the summary is not labelled with the preset revision");
}

// The badge is a condition on two numbers, and it goes the moment they part.
void badgeOnlyAtTheSiderealMatch() {
    auto badgeShows = [](double period) {
        const auto scene = sceneWith(O{1361.0, period, 42164000.0});
        const auto summary = orbitSummary(scene);
        return summary.geostationary && hasLine(orbitSummaryLines(summary), kGeostationaryBadge);
    };
    require(badgeShows(core::PlanetSiderealRotationS), "no Geostationary badge at the sidereal match");
    require(!badgeShows(core::PlanetSiderealRotationS - 0.1),
            "the badge survived a tenth of a second short of the match");
    require(!badgeShows(core::PlanetSiderealRotationS + 0.1),
            "the badge survived a tenth of a second past the match");
    require(!badgeShows(5570.0), "the badge appeared for an ISS-like period");
    require(!badgeShows(core::PlanetSiderealRotationS * 2.0),
            "a synchronous-with-half-the-rotation period read as Geostationary");
    // A typed number that round trips through the document still matches.
    I scene(2, 2);
    scene.setHullType(I::HullType::BareSkin);
    scene.setOrbit(O{1361.0, core::PlanetSiderealRotationS, 42164000.0});
    I reloaded(1, 1);
    reloaded.deserialize(scene.serialize());
    require(orbitSummary(reloaded).geostationary,
            "the badge was lost through a document round trip");
    const auto skyless = orbitSummary(sceneWith(std::nullopt));
    require(!skyless.authored && !skyless.geostationary, "a badge appeared with no orbit at all");
    require(!hasLine(orbitSummaryLines(skyless), kGeostationaryBadge),
            "the badge was drawn for a skyless Installation");
}

// Every edit the author can make moves what it should and nothing else.
void derivedValuesFollowEveryEdit() {
    station::editor::EditorSession session;
    session.setHullType(I::HullType::BareSkin);
    session.setOrbit(O{1361.0, 5570.0, 6791000.0});
    const auto low = orbitSummary(session.installation());
    require(low.authored && low.geostationary == false, "the first edit was not read back");

    session.setOrbit(O{1361.0, 5570.0, 42164000.0});
    const auto high = orbitSummary(session.installation());
    require(high.radiusFromPlanetCentreM == 42164000.0, "radius edit not read back");
    require(high.eclipseDurationS < low.eclipseDurationS,
            "raising the orbital radius did not shorten the eclipse");
    require(near(high.groundDriftDegPerDay, low.groundDriftDegPerDay, 1e-9),
            "a radius edit moved Ground drift, which does not depend on radius");

    session.setOrbit(O{1361.0, 6000.0, 42164000.0});
    const auto slower = orbitSummary(session.installation());
    require(!near(slower.groundDriftDegPerDay, high.groundDriftDegPerDay, 1.0),
            "an Orbital period edit did not move Ground drift");
    require(!near(slower.eclipseDurationS, high.eclipseDurationS, 1.0),
            "an Orbital period edit did not move the eclipse duration");
    require(near(slower.sunlitDurationS + slower.eclipseDurationS, 6000.0, 1e-9),
            "the durations stopped summing to the new Orbital period");

    session.setOrbit(O{1361.0, 6000.0, 42164000.0, 0.3, 0.5});
    const auto tilted = orbitSummary(session.installation());
    require(tilted.eclipseDurationS < slower.eclipseDurationS,
            "growing Beta did not shorten the eclipse");
    require(near(tilted.groundDriftDegPerDay, slower.groundDriftDegPerDay, 1e-9),
            "a Beta edit moved Ground drift");

    session.setOrbit(O{1361.0, 6000.0, 42164000.0, 0.3, 0.5, 900.0});
    const auto shifted = orbitSummary(session.installation());
    require(shifted.phaseOffsetS == 900.0, "phase offset edit not read back");
    require(near(shifted.eclipseDurationS, tilted.eclipseDurationS, 1e-12),
            "the phase offset resized the eclipse instead of moving it");

    session.setOrbit(O{1361.0, core::PlanetSiderealRotationS, 42164000.0, 0.3, 0.0, 0.0});
    const auto synchronous = orbitSummary(session.installation());
    require(synchronous.geostationary, "editing into the sidereal period did not raise the badge");
    require(near(synchronous.groundDriftDegPerDay, 0.0, 1e-9),
            "a Geostationary orbit still reported drift");

    session.setOrbit(std::nullopt);
    const auto cleared = orbitSummary(session.installation());
    require(!cleared.authored, "clearing the orbit left the summary authored");
    require(std::isnan(cleared.eclipseDurationS) && std::isnan(cleared.sunlitDurationS)
                && std::isnan(cleared.groundDriftDegPerDay),
            "a cleared orbit reported derived numbers instead of gaps");
    require(hasLine(orbitSummaryLines(cleared), "No Orbit authored"),
            "a cleared orbit was not explained in the summary");
}

// The trace is the evaluator's published exposure at the instant it is
// stamped, not a re-derivation that could drift from it.
void traceMatchesThePublishedEvaluator() {
    // A short Orbital period so a whole day/night cycle fits inside the
    // sixty-second window the graph keeps.
    const auto scene = sceneWith(O{1361.0, 20.0, 6791000.0});
    SimulationPlayback playback(std::nullopt);
    playback.reset(scene);
    playback.togglePlaying();
    playback.setSpeedIndex(4);
    for (int i = 0; i < 250; ++i) playback.update(0.1);
    playback.togglePlaying();
    const auto trace = playback.exposureHistory();
    require(!trace.empty(), "no Stellar exposure trace was published");
    const auto& evaluator = playback.runtime()->model().exposureEvaluator();
    require(!evaluator.dark() && !playback.runtime()->model().solarCycleOverrides(),
            "the trace ran under a sky that was not being flown");
    bool sunlit = false;
    bool shadowed = false;
    for (const auto& sample : trace) {
        require(std::isfinite(sample.exposure),
                "a live sky published an unobserved trace sample");
        const double expected = 1.0 - evaluator.evaluate(sample.seconds).eclipse;
        require(sample.exposure == expected,
                "the trace disagrees with the evaluator at its own simulated time");
        sunlit = sunlit || sample.exposure > 0.5;
        shadowed = shadowed || sample.exposure <= 0.5;
    }
    require(sunlit && shadowed, "the trace never crossed a sunrise or a sunset");
    require(trace.back().seconds - trace.front().seconds <= 60.0 + 1e-9,
            "the trace is not bounded to the graph's one-minute window");

    const auto geometry = exposureTraceGeometry(trace);
    require(!geometry.eclipseBands.empty(), "an eclipsed trace shaded no band");
    require(geometry.runs.size() == 1, "an uninterrupted trace was broken into runs");
    require(geometry.runs[0].size() == trace.size(), "the trace run dropped observations");
    for (const auto& [from, to] : geometry.eclipseBands)
        require(to > from, "an eclipse band was drawn with no width");
    for (const auto& sample : trace) {
        if (sample.exposure <= 0.0)
            require(bandCovers(geometry.eclipseBands, sample.seconds, true),
                    "an eclipsed observation was left outside every shaded band");
        else
            require(!bandCovers(geometry.eclipseBands, sample.seconds, false),
                    "a sunlit observation was shaded as though eclipsed");
    }
}

// No sky is not zero exposure. The author sees a gap and is told why.
void unavailableObservationsAreGaps() {
    const auto scene = sceneWith(std::nullopt);
    SimulationPlayback playback(std::nullopt);
    playback.reset(scene);
    playback.togglePlaying();
    playback.setSpeedIndex(4);
    for (int i = 0; i < 40; ++i) playback.update(0.1);
    playback.togglePlaying();
    const auto trace = playback.exposureHistory();
    require(!trace.empty(), "a skyless run published no trace samples at all");
    for (const auto& sample : trace)
        require(std::isnan(sample.exposure),
                "a skyless run invented a zero exposure instead of a gap");
    const auto geometry = exposureTraceGeometry(trace);
    require(geometry.eclipseBands.empty(), "a gap was shaded as an eclipse");
    require(geometry.runs.empty(), "a gap was drawn as a trace");

    // A gap in the middle of an otherwise live trace splits it and stops
    // the band dead rather than sloping it to zero.
    const std::vector<ExposureSample> broken{{0.0, 1.0}, {1.0, 0.0}, {2.0, kNaN},
                                           {3.0, 0.0}, {4.0, 1.0}};
    const auto split = exposureTraceGeometry(broken);
    require(split.runs.size() == 2, "a gap did not split the trace into two runs");
    require(split.eclipseBands.size() == 2, "a gap did not close the eclipse band");
    require(near(split.eclipseBands[0].first, 1.0, 1e-12)
                && near(split.eclipseBands[0].second, 1.0, 1e-12),
            "the band before the gap was stretched past the last observation that saw shadow");
    require(near(split.eclipseBands[1].first, 3.0, 1e-12)
                && near(split.eclipseBands[1].second, 4.0, 1e-12),
            "the band after the gap did not run to the sunlit observation that closed it");
    require(split.runs[0].size() == 2 && split.runs[1].size() == 2,
            "the split runs did not keep their own observations");
}

// A hand-authored solar profile owns the whole stellar term, so the
// orbital sky is not being flown and neither readout may claim it is.
void aProfileOverrideLeavesNoSkyToReport() {
    const auto scene = sceneWith(O{1361.0, 5570.0, 6791000.0});
    auto expansion = nsi::systems::expandCellAtmosphere(scene, true);
    expansion.thermal.solarCycle = station::ProfileSettings{1.0};
    nsi::systems::CellAtmosphereTransport transport(std::move(expansion),
                                                   nsi::systems::CellThermalMode::LocalStructure);
    require(transport.solarCycleOverrides(), "the authored profile did not take the override");
    const auto summary = orbitSummary(scene, &transport);
    require(summary.authored && summary.overridden, "the override was not reported");
    require(std::isnan(summary.eclipseDurationS) && std::isnan(summary.sunlitDurationS)
                && std::isnan(summary.groundDriftDegPerDay),
            "derived orbital numbers were shown while a profile owned the sky");
    require(!summary.geostationary, "the badge was raised under a profile override");
    require(hasLine(orbitSummaryLines(summary), "unavailable"),
            "the override was not explained in the summary");
    require(hasLine(orbitSummaryLines(summary), "solar profile"),
            "the summary did not name the profile as the reason");
    // The authored inputs are still shown: they are what the author wrote.
    require(summary.stellarIrradianceWPerM2 == 1361.0 && summary.albedo == 0.3,
            "an override hid the authored inputs");
}

// The graph itself: the trace is drawn on the hull temperature plot, on
// its own axis, and labelled with the revisions that produced it.
void graphCarriesTheLabelledTrace() {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1400, 900};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    const auto scene = sceneWith(O{1361.0, 20.0, 6791000.0});
    SimulationPlayback playback(std::nullopt);
    playback.reset(scene);
    playback.togglePlaying();
    playback.setSpeedIndex(4);
    for (int i = 0; i < 40; ++i) playback.update(0.1);
    playback.togglePlaying();

    StructuralPanelState view;
    // The docking branch hides a freshly docked window for a frame while it
    // builds the host window, so every render assertion here is made on the
    // second frame of the pair.
    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize({1200, 800});
        ImGui::Begin("Structure inspector");
        drawStructuralPanel(playback, {0, 0, 0}, view);
        if (frame == 1) {
            auto* plot = ImPlot::GetPlot("###StructuralHistory");
            require(plot != nullptr, "the thermal graph was not drawn");
            require(plot->Axes[ImAxis_Y2].Enabled, "the exposure trace has no secondary axis");
            const int legend = plot->Items.GetLegendCount();
            require(legend == 7, "the exposure trace did not add its legend entry to the thermal graph");
            bool labelled = false;
            for (int i = 0; i < legend; ++i) {
                const std::string text(plot->Items.GetLegendLabel(i));
                if (text.find("Stellar exposure") == std::string::npos) continue;
                labelled = text.find(std::string(station::NativeThermalModelRevision)) != std::string::npos
                      && text.find(std::string(core::NativeThermalPresetRevision)) != std::string::npos;
            }
            require(labelled, "the trace is not labelled with the thermal model and preset revision");
        }
        ImGui::End();
        ImGui::Render();
    }
    require(ImGui::GetDrawData()->TotalVtxCount > 0, "the thermal graph failed to render");

    // The summary panel draws too, under both a live and a cleared sky.
    for (const auto& shown : {sceneWith(O{1361.0, core::PlanetSiderealRotationS, 42164000.0}),
                             sceneWith(std::nullopt)}) {
        for (int frame = 0; frame < 2; ++frame) {
            ImGui::NewFrame();
            ImGui::Begin("Inspector");
            drawOrbitSummary(shown, playback.runtime() ? &playback.runtime()->model() : nullptr);
            ImGui::End();
            ImGui::Render();
        }
        require(ImGui::GetDrawData()->TotalVtxCount > 0, "the orbit summary failed to render");
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}
} // namespace

int main() {
    try {
        summaryFormatsNumericValues();
        summaryShowsAuthoredBesideDerived();
        badgeOnlyAtTheSiderealMatch();
        derivedValuesFollowEveryEdit();
        traceMatchesThePublishedEvaluator();
        unavailableObservationsAreGaps();
        aProfileOverrideLeavesNoSkyToReport();
        graphCarriesTheLabelledTrace();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "orbit summary and exposure trace ok\n";
    return 0;
}
