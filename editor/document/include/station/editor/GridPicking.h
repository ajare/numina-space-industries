#pragma once
#include <nsi/core/Installation.h>
#include <algorithm>
#include <cmath>
#include <optional>

namespace station::editor {
// Coordinates are in Cell units. Boundary edges are selectable; outside
// the rectangular footprint there is no hit. Ties favour horizontal edges.
inline std::optional<std::size_t> nearestWall(const nsi::core::Installation::Level& level,
                                             double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>level.width() || y>level.depth())
        return std::nullopt;
    auto cx=static_cast<std::uint32_t>(std::min(std::floor(x),double(level.width()-1)));
    auto cy=static_cast<std::uint32_t>(std::min(std::floor(y),double(level.depth()-1)));
    auto vx=static_cast<std::uint32_t>(std::round(x));
    auto hy=static_cast<std::uint32_t>(std::round(y));
    return std::abs(y-hy)<=std::abs(x-vx) ? level.horizontalWall(cx,hy) : level.verticalWall(vx,cy);
}
}
