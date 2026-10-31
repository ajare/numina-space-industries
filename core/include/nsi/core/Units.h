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

// The circle constant, shared by every orbital derivation in core so the
// period and the synchronous height are computed from one number rather
// than two that have to be kept in step.
inline constexpr double kPi{3.14159265358979323846};

// The world an Installation orbits is authored now: Orbit::planetRadiusM,
// with a catalogue of the eight planets in WorldPresets.h. This constant
// is the default a record falls back to when it names no world, which is
// truthful rather than a substitute - every document written before the
// field existed was orbiting exactly this. It still bounds the authored
// orbital radius (a Hull cannot sit inside the ground) and still sets how
// much of the sky the planet fills. Earth-like, NASA NSSDCA mean radius.
inline constexpr double PlanetRadiusM{6371000.0};

// The same world's mass in kilograms. With the constant below this is what
// turns an authored height into an Orbital period, so it is no longer a
// number the model can do without. Earth-like, NASA NSSDCA.
inline constexpr double PlanetMassKg{5.9724e24};

// Newton's gravitational constant, CODATA 2018. Only the product G*M is
// ever physically meaningful, and for Earth that product is known far more
// precisely than either factor alone; writing the published mass to five or
// six significant figures keeps every derived period within about a tenth
// of a second of the real thing, which is far inside anything this thermal
// model can notice.
inline constexpr double GravitationalConstantM3PerKgS2{6.67430e-11};

// The same world's sidereal rotation period in seconds, Earth-like. ADR-0008
// settles that the planet's rotation drives nothing in the thermal model:
// the shadow cylinder is fixed in the inertial frame, so no thermal
// quantity may read it. It exists for the author-facing derived readouts
// that cannot be stated without it - Ground drift and the synchronous
// height - and since the world itself is authored it is authored alongside
// it, rather than being hardcoded Earth under a Jovian sky.
inline constexpr double PlanetSiderealRotationS{86164.0};

} // namespace nsi::core
