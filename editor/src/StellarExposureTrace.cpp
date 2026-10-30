#include "StellarExposureTrace.h"

#include "implot.h"

#include <cmath>

namespace station::editor {
namespace {
// A warm line against the cool temperature curves: the cause has to be
// readable as something other than the effect it is drawn beside.
constexpr ImU32 kTraceColour{IM_COL32(238, 198, 92, 255)};
// The Eclipse band sits behind everything and reads as shade rather than
// as data, which is what a shadow is.
constexpr ImU32 kEclipseBandColour{IM_COL32(38, 58, 92, 120)};
constexpr float kTraceWeight{1.5f};
// A lone observation would otherwise be a zero-length polyline.
constexpr float kLoneObservationHalfWidth{1.5f};

bool observed(const ExposureSample& sample) { return std::isfinite(sample.exposure); }
bool eclipsed(const ExposureSample& sample) { return observed(sample) && sample.exposure <= 0.0; }

// A band spanning the whole visible height of the plot, so the shadow is
// read against the temperature curves rather than measured off them.
void shadeBand(std::pair<double, double> band) {
    const auto [from, to] = band;
    if (!(to > from)) return;
    const auto limits = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1);
    const ImVec2 start = ImPlot::PlotToPixels(from, limits.Y.Min, ImAxis_X1, ImAxis_Y1);
    const ImVec2 end = ImPlot::PlotToPixels(to, limits.Y.Max, ImAxis_X1, ImAxis_Y1);
    ImPlot::GetPlotDrawList()->AddRectFilled(ImVec2{start.x, end.y}, ImVec2{end.x, start.y},
                                           kEclipseBandColour);
}
} // namespace

std::string exposureTraceLabel(std::string_view modelRevision, std::string_view presetRevision) {
    std::string label = "Stellar exposure (model ";
    label += modelRevision;
    label += ", preset ";
    label += presetRevision;
    label += ")";
    return label;
}

ExposureTraceGeometry exposureTraceGeometry(std::span<const ExposureSample> trace) {
    ExposureTraceGeometry geometry;
    bool inBand = false;
    double bandStart = 0.0;
    double bandEnd = 0.0;
    std::vector<std::pair<double, double>> run;
    auto closeRun = [&] {
        if (!run.empty()) {
            geometry.runs.push_back(std::move(run));
            run = {};
        }
    };
    for (const auto& sample : trace) {
        if (eclipsed(sample)) {
            if (!inBand) {
                inBand = true;
                bandStart = sample.seconds;
            }
            bandEnd = sample.seconds;
        } else if (inBand) {
            // A sunlit observation says the shadow lasted up to this
            // instant. An unobserved sample says nothing past the last one
            // that did, so the gap stays unshaded rather than being drawn
            // as a shadow nobody measured.
            geometry.eclipseBands.emplace_back(bandStart, observed(sample) ? sample.seconds : bandEnd);
            inBand = false;
        }
        if (!observed(sample)) {
            closeRun();
            continue;
        }
        run.emplace_back(sample.seconds, sample.exposure);
    }
    if (inBand) geometry.eclipseBands.emplace_back(bandStart, bandEnd);
    closeRun();
    return geometry;
}

void drawStellarExposureTrace(std::span<const ExposureSample> trace, std::string_view label) {
    if (trace.empty()) return;
    // Register the legend entry before drawing anything over it, and give
    // it the exact colour the line is drawn in so the swatch is not a
    // guess.
    const std::string text(label);
    ImPlot::PushStyleColor(ImPlotCol_Line, ImGui::ColorConvertU32ToFloat4(kTraceColour));
    ImPlot::PlotDummy(text.c_str());
    ImPlot::PopStyleColor();

    const auto geometry = exposureTraceGeometry(trace);
    ImPlot::PushPlotClipRect();
    auto* draw = ImPlot::GetPlotDrawList();
    for (const auto& band : geometry.eclipseBands) shadeBand(band);
    for (const auto& run : geometry.runs) {
        if (run.size() == 1) {
            const ImVec2 here =
                ImPlot::PlotToPixels(run[0].first, run[0].second, ImAxis_X1, ImAxis_Y2);
            draw->AddLine(ImVec2{here.x - kLoneObservationHalfWidth, here.y},
                         ImVec2{here.x + kLoneObservationHalfWidth, here.y}, kTraceColour,
                         kTraceWeight);
            continue;
        }
        // Each step holds the previous observation until the next one
        // arrives, then jumps: exposure changes at an instant, not over
        // the interval between samples.
        ImVec2 previous =
            ImPlot::PlotToPixels(run[0].first, run[0].second, ImAxis_X1, ImAxis_Y2);
        for (std::size_t i = 1; i < run.size(); ++i) {
            const ImVec2 here =
                ImPlot::PlotToPixels(run[i].first, run[i].second, ImAxis_X1, ImAxis_Y2);
            const ImVec2 corner{here.x, previous.y};
            draw->AddLine(previous, corner, kTraceColour, kTraceWeight);
            draw->AddLine(corner, here, kTraceColour, kTraceWeight);
            previous = here;
        }
    }
    ImPlot::PopPlotClipRect();
}
} // namespace station::editor
