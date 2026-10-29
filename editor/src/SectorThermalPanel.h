#pragma once
#include "SectorThermal.h"
#include "TemperatureSample.h"
#include <span>
#include <functional>
namespace station::editor {
void drawSectorThermal(const SectorThermal&, std::span<const TemperatureSample> history = {},
    const std::function<void()>& drawConfiguration = {});
}
