#pragma once

#include "station/editor/Installation.h"

#include <cstddef>
#include <vector>

// Screen-space floor tiles for the selected Level.
struct OverviewCell {
    float x;
    float y;
    float halfSize;
};

// One solid square per Interior Cell; no geometry for Void Cells or grid lines.
std::vector<OverviewCell> buildOverviewCells(
    const station::editor::Installation& station, std::size_t width,
    std::size_t height, std::size_t level = 0);
