#include "OverviewGeometry.h"

#include <algorithm>

std::vector<OverviewCell> buildOverviewCells(
    const station::editor::Installation& station, std::size_t width,
    std::size_t height, std::size_t level_index) {
    std::vector<OverviewCell> cells;
    if (level_index >= station.levelCount() || width == 0 || height == 0) return cells;
    const auto& level = station.level(level_index);
    if (level.width() == 0 || level.depth() == 0) return cells;

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float columns = static_cast<float>(level.width());
    const float rows = static_cast<float>(level.depth());
    // Keep square Cells and fit the entire Level, even in a tiny viewport.
    const float size = std::min(w / columns, h / rows);
    const float offset_x = (w - size * columns) * 0.5f;
    const float offset_y = (h - size * rows) * 0.5f;

    cells.reserve(level.interiorCount());
    for (std::uint32_t y = 0; y < level.depth(); ++y) {
        for (std::uint32_t x = 0; x < level.width(); ++x) {
            if (level.cell(x, y) == station::editor::SectorType::Interior) {
                cells.push_back({offset_x + size * (static_cast<float>(x) + 0.5f),
                                 offset_y + size * (static_cast<float>(y) + 0.5f),
                                 size * 0.5f});
            }
        }
    }
    return cells;
}
