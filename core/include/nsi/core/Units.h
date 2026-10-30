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

// A Door occupies one Wall edge but its clear opening is smaller than that
// edge's wall surface. These dimensions do not resize the grid or Sectors.
inline constexpr double DoorWidthM{0.9};
inline constexpr double DoorHeightM{2.0};
inline constexpr double DoorOpeningAreaM2{DoorWidthM * DoorHeightM};

// Derived footprint and single-Level volume of one Cell.
inline constexpr double CellAreaM2{CellWidthM * CellDepthM};
inline constexpr double CellVolumeM3{CellAreaM2 * LevelHeightM};

// Every Installation orbits one world; there is no planet catalogue. The
// radius bounds the authored orbital radius (a Hull cannot sit inside the
// ground) and sets how much of the sky the planet fills. Earth-like.
inline constexpr double PlanetRadiusM{6371000.0};

// The same world's sidereal rotation period in seconds, Earth-like. ADR-0008
// settles that the planet's rotation drives nothing in the thermal model:
// the shadow cylinder is fixed in the inertial frame, so this is
// deliberately not an authored input and no thermal quantity may read it.
// It exists for one purpose, the author-facing derived readouts that cannot
// be stated without it - the Geostationary condition and Ground drift.
inline constexpr double PlanetSiderealRotationS{86164.0};

} // namespace nsi::core
