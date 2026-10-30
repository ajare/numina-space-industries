#pragma once

#include "ExposureSample.h"

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace station::editor {
// The legend label for the Stellar exposure trace. It carries the thermal
// model and preset revision so a curve read off a saved graph is
// attributable to the physics that produced it: a changed physical default
// then shows up as a changed label rather than as a data migration the
// author somehow caused.
std::string exposureTraceLabel(std::string_view modelRevision, std::string_view presetRevision);

// The trace resolved to what it would draw, in plot units rather than
// pixels, so the bands and the gaps can be asserted without a plot under
// them.
struct ExposureTraceGeometry {
    // Eclipse bands as [from, to] seconds. A band runs from the first
    // eclipsed observation to the next one that says otherwise, which is
    // the same sample-and-hold the staircase uses.
    std::vector<std::pair<double, double>> eclipseBands;
    // The staircase as (seconds, exposure) vertices, one run per
    // uninterrupted stretch of observation. An unobserved sample closes a
    // run and opens the next one, so a gap is a gap and never a slope to
    // zero.
    std::vector<std::vector<std::pair<double, double>>> runs;
};

// Resolve the trace without drawing it.
ExposureTraceGeometry exposureTraceGeometry(std::span<const ExposureSample> trace);

// Draw the committed exposure trace and its Eclipse bands into the plot
// that is currently open. The trace is a staircase on the plot's secondary
// Y axis, 1 in sun and 0 in Eclipse, so it reads against the hull
// temperature curves without being squashed by their scale. Samples that
// are NaN - no Orbit authored, or a hand-authored solar profile owning the
// stellar term - break the line rather than being drawn as zero, and no
// band is shaded across them.
//
// Call between BeginPlot and EndPlot, after the Setup calls.
void drawStellarExposureTrace(std::span<const ExposureSample> trace, std::string_view label);
} // namespace station::editor
