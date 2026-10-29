#pragma once
#include "SectorThermal.h"
#include "TemperatureSample.h"
#include <span>
namespace station::editor {
void drawSectorThermal(const SectorThermal&, std::span<const TemperatureSample> history = {});
}
