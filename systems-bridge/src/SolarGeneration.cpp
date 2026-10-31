#include <nsi/systems/SolarGeneration.h>
#include <cmath>
#include <limits>

namespace nsi::systems {
bool updateSystemsWithAuthoredSky(station::StationSystemsRuntime& runtime,
    const StellarExposureEvaluator& sky, double seconds) {
    if (runtime.updateMode()!=station::StationSystemsUpdateMode::Variable
        || !std::isfinite(seconds) || seconds<=0
        || seconds>std::numeric_limits<float>::max()) return false;
    const double midpoint=runtime.snapshot().elapsedSeconds+seconds/2;
    // Perfect star-tracking: no hull incidence or horizon gate. Planetary IR
    // photons (250 K: ~0.107 eV) are below even the Ge subcell's 0.66 eV
    // bandgap, so cannot generate photocurrent. Reflected sunlight is excluded
    // under perfect tracking too; reconsider albedo only if tracking is relaxed.
    const double direct=sky.stellarIrradianceWPerM2()*(1-sky.evaluate(midpoint).eclipse);
    return runtime.setDirectSolarIrradianceWPerM2(direct) && runtime.update(seconds);
}
} // namespace nsi::systems
