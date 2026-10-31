#include "SolarArrayControls.h"
#include "station/editor/EditorSession.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace station::editor {
namespace {
// The same shade the Stellar exposure trace uses for a shadow, so an
// Eclipse reads as one thing across both graphs: a cause lying behind the
// curve, not a series competing with it.
constexpr ImU32 kEclipseBandColour{IM_COL32(38, 58, 92, 120)};

bool observable(const SolarGenerationSample& sample) { return std::isfinite(sample.availableKW); }

// A band spanning the whole visible height of the plot, so the shadow is
// read against the generation curve rather than measured off it.
void shadeEclipseBand(std::pair<double, double> band) {
    const auto [from, to] = band;
    if (!(to > from)) return;
    const auto limits = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1);
    const ImVec2 start = ImPlot::PlotToPixels(from, limits.Y.Min, ImAxis_X1, ImAxis_Y1);
    const ImVec2 end = ImPlot::PlotToPixels(to, limits.Y.Max, ImAxis_X1, ImAxis_Y1);
    ImPlot::GetPlotDrawList()->AddRectFilled(ImVec2{start.x, end.y}, ImVec2{end.x, start.y},
                                            kEclipseBandColour);
}

// The rolling last-minute generation graph, on the same terms as the
// Atmosphere Pressure and Thermal Temperature plots beside it: simulation
// time along the bottom, a sixty second window pinned to the newest
// observation, the value axis fitted to what is actually in the window.
void drawSolarGenerationGraph(std::span<const SolarGenerationSample> history) {
    if (history.empty()) {
        ImGui::TextUnformatted("Generation history unavailable.");
        return;
    }
    // Every sample a gap - an array the systems side reports disabled, or
    // no Orbit authored - leaves nothing honest to draw. Say so instead of
    // showing an empty frame.
    if (!std::any_of(history.begin(), history.end(), observable)) {
        ImGui::TextUnformatted("No generation to plot while the array is disabled or there is no star.");
        return;
    }
    if (!ImPlot::BeginPlot("Solar generation — last minute###SolarGeneration", ImVec2(-1, 180),
                          ImPlotFlags_NoLegend))
        return;
    const double now = history.back().seconds;
    ImPlot::SetupAxes("Simulation time (s)", "Generation (kW)", ImPlotAxisFlags_None,
                     ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisLimits(ImAxis_X1, std::max(0.0, now - 60.0), std::max(60.0, now),
                           ImGuiCond_Always);
    ImPlot::PushPlotClipRect();
    for (const auto& band : solarEclipseBands(history)) shadeEclipseBand(band);
    ImPlot::PopPlotClipRect();
    ImPlot::PlotLineG("Generation", [](int index, void* data) {
        const auto& sample = (*static_cast<std::span<const SolarGenerationSample>*>(data))[index];
        return ImPlotPoint(sample.seconds, sample.availableKW);
    }, &history, static_cast<int>(history.size()));
    ImPlot::EndPlot();
}
} // namespace

std::vector<std::pair<double, double>> solarEclipseBands(
    std::span<const SolarGenerationSample> generation) {
    std::vector<std::pair<double, double>> bands;
    bool inBand = false;
    double from = 0.0;
    double to = 0.0;
    auto close = [&] {
        // Only a band with width is a band. A lone eclipsed observation
        // touches zero at an instant, and the line drawn between samples is
        // interpolated rather than held, so there is no stretch to shade.
        if (inBand && to > from) bands.push_back({from, to});
        inBand = false;
    };
    for (const auto& sample : generation) {
        // An unobservable sample closes the band rather than extending it:
        // a dead array is not night, and a shadow may not be carried across
        // a stretch where nothing was read.
        if (!observable(sample)) {
            close();
            continue;
        }
        if (sample.eclipse) {
            if (!inBand) {
                inBand = true;
                from = sample.seconds;
            }
            to = sample.seconds;
        } else
            close();
    }
    close();
    return bands;
}

bool drawSolarArrayPanel(EditorSession& session, const std::optional<StationSystemsSnapshot>& snapshot,
                        std::span<const SolarGenerationSample> generation) {
    auto array=session.installation().solarArray();
    if (!array) {
        ImGui::BeginDisabled(!session.editingEnabled());
        const bool added=ImGui::Button("Add solar array");
        if (added) session.setSolarArray(Installation::SolarArray{});
        ImGui::EndDisabled();
        return added;
    }
    // Missing Orbit is semantic absence. Never infer it from the irradiance
    // scalar, which is also zero in eclipse. Disabled takes precedence.
    // #69: Eclipse comes from the published systems-side shadow state and
    // never from zero output - a tracking failure and a fully worn blanket
    // both produce a zero that is not night.
    const auto* source=snapshot && !snapshot->power.sources.empty() ? &snapshot->power.sources.front() : nullptr;
    // Below half a tenth, "%.1f" prints a generating array as "0.0 kW",
    // which reads as a dead panel. Say it is under the readable floor
    // instead of rounding a measurement away.
    constexpr double kReadableFloorKW=0.05;
    char title[128];
    if (source && source->disabled)
        std::snprintf(title,sizeof title,"Solar array — Disabled");
    else if (!session.installation().orbit()) std::snprintf(title,sizeof title,"Solar array — No star");
    else if (!source) std::snprintf(title,sizeof title,"Solar array — Unavailable");
    else if (source->eclipse) std::snprintf(title,sizeof title,"Solar array — Eclipse");
    else if (source->availableKW<kReadableFloorKW)
        std::snprintf(title,sizeof title,"Solar array — <0.1 kW");
    else std::snprintf(title,sizeof title,"Solar array — %.1f kW",source->availableKW);
    // Stable ID: changing power/state must not reset the user's collapsed state.
    const std::string label=std::string(title)+"###solar-array";
    if (!ImGui::CollapsingHeader(label.c_str(),ImGuiTreeNodeFlags_DefaultOpen)) return false;
    bool changed=false;
    ImGui::BeginDisabled(!session.editingEnabled());
    if (ImGui::InputDouble("Blanket area (m2)",&array->areaM2,1,10,"%.1f")) {
        if (std::isfinite(array->areaM2) && array->areaM2>=0) {
            session.setSolarArray(array); changed=true;
        } else array=session.installation().solarArray();
    }
    // The catalogue's display names live in the systems library; the Editor
    // keeps no second copy of the wording (#73). ImGui's Combo wants one
    // '\0'-delimited block instead of a char const*[], so join them once.
    static const std::string kCellTechnologyItems=[] {
        std::string items;
        for (const auto* label : station::SolarCellTechnologyLabels) {
            items += label;
            items += '\0';
        }
        return items;
    }();
    int technology=static_cast<int>(array->cellTechnology);
    if (ImGui::Combo("Cell technology",&technology,kCellTechnologyItems.c_str())) {
        array->cellTechnology=static_cast<Installation::SolarCellTechnology>(technology);
        session.setSolarArray(array); changed=true;
    }
    ImGui::TextUnformatted("Power is not connected to station consumers.");
    if (ImGui::Button("Remove solar array")) { session.setSolarArray(std::nullopt); changed=true; }
    ImGui::EndDisabled();
    // The header above is one instant; this is the same number over the last
    // minute of it, so an orbit's day and night read as a shape rather than
    // being inferred from a title that changes twice a period.
    drawSolarGenerationGraph(generation);
    return changed;
}
}
