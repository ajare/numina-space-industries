#pragma once

namespace nsi::core {

// Physical scale of the grid. One Cell is a 2 m x 2 m footprint; consecutive
// Levels are 3 m apart vertically. This is the single authoritative mapping
// from grid units to metres for the whole project: compartment volumes, and
// therefore every pressure derived from them, scale with these numbers, so
// changing one re-sizes every authored atmosphere.
inline constexpr double CellWidthM{2.0};
inline constexpr double CellDepthM{2.0};
inline constexpr double LevelHeightM{3.0};

// Derived footprint and single-Level volume of one Cell.
inline constexpr double CellAreaM2{CellWidthM * CellDepthM};
inline constexpr double CellVolumeM3{CellAreaM2 * LevelHeightM};

} // namespace nsi::core
