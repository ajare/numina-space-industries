#pragma once
#include <station/StationSystems.h>
#include "SolarGenerationSample.h"
#include <optional>
#include <span>
#include <utility>
#include <vector>
namespace station::editor {
class EditorSession;
// Returns true only when the authored choice changes.
bool drawSolarArrayControls(EditorSession& session);
// The rolling last-minute generation history drawn inside the header. Pass
// an empty span where there is no preview matching the document, so the
// graph never draws a curve under a header reading Unavailable.
bool drawSolarArrayPanel(EditorSession&, const std::optional<StationSystemsSnapshot>&,
    std::span<const SolarGenerationSample> generation = {});
// The Eclipse stretches in a generation history, as [from, to] seconds.
// A band runs from one eclipsed observation to the next and closes at the
// first sample that says otherwise. An unobservable sample - the array
// switched off, or no star to work under - closes the band rather than
// extending it, because that stretch is no reading at all rather than
// night. A lone eclipsed observation yields a zero-width band and is
// dropped: there is nothing to shade.
std::vector<std::pair<double, double>> solarEclipseBands(
    std::span<const SolarGenerationSample> generation);
}
