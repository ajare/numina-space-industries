#pragma once
#include <nsi/systems/StellarExposure.h>
#include <station/StationSystemsRuntime.h>

namespace nsi::systems {
// Advance one variable-duration systems tick with the authored sky sampled at
// its midpoint, using the runtime's committed clock (never wall time). Construct
// the evaluator from the Orbit at Reset, or default-construct for no Orbit.
// One station-wide W/m² scalar crosses the library boundary; both tracking
// wings see the same sky. Legacy arrays still read only their solar cycle.
// Fixed-mode and invalid durations are refused before replacing the input.
bool updateSystemsWithAuthoredSky(station::StationSystemsRuntime&,
    const StellarExposureEvaluator&, double seconds);
} // namespace nsi::systems
