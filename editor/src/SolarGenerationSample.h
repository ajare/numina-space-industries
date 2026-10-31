#pragma once

#include <limits>

namespace station::editor {
// One committed Solar generation observation on the Solar array graph: the
// unclipped power the array is making at that simulated instant, in kW,
// carried beside the systems-side shadow state that explains a zero.
//
// NaN means there is no observation to plot - no array, no snapshot, no
// authored Orbit, or the array switched off. It renders as a gap. Those
// are exactly the states the header spells out in words instead of a
// figure, and drawing them as a plotted zero would claim a measurement the
// header refused to print.
//
// Eclipse keeps its zero. The array is authored, enabled and under a sky;
// nothing arriving is a true reading of that sky, and the day/night shape
// is the point of the graph. The shadow is shaded behind the line so the
// zero reads as night rather than as a dead panel (#69).
struct SolarGenerationSample {
    double seconds{};
    double availableKW{std::numeric_limits<double>::quiet_NaN()};
    bool eclipse{};
};
} // namespace station::editor
