#pragma once
#include "SectorThermal.h"
#include "PressureSample.h"
#include "SectorAtmosphere.h"
#include <span>

namespace station::editor {
void drawSectorAtmosphere(const SectorThermal&,std::span<const PressureSample> history = {},
    std::span<const AtmosphereSample> atmosphereHistory = {});
} // namespace station::editor
