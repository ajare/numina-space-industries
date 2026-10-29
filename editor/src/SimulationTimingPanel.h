#pragma once
#include "SimulationStepTiming.h"

namespace station::editor {
// Left-inspector timing chart; never records samples from the render loop.
void drawSimulationTiming(const SimulationStepTiming&);
} // namespace station::editor
