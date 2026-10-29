#include <nsi/systems/StellarExposure.h>

#include <algorithm>
#include <cmath>

namespace nsi::systems {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
// Beta is the angle between the star and the orbital plane, so it lives in
// [-pi/2, pi/2]. Anything at or beyond the orbit pole has no sweep to make
// and is refused as dark rather than dividing by a vanishing cos(beta).
constexpr double kMinCosBeta = 1e-9;
}

StellarExposureEvaluator::StellarExposureEvaluator(
    const core::Installation::Orbit& orbit, double betaRadians) noexcept {
    if (!std::isfinite(orbit.stellarIrradianceWPerM2) || orbit.stellarIrradianceWPerM2 <= 0.0
        || !std::isfinite(orbit.orbitalPeriodS) || orbit.orbitalPeriodS <= 0.0
        || !std::isfinite(orbit.radiusFromPlanetCentreM)
        || orbit.radiusFromPlanetCentreM <= core::PlanetRadiusM
        || !std::isfinite(betaRadians)) return; // dark_ stays true
    const double beta = std::fmod(betaRadians, kTwoPi);
    const double cosBeta = std::cos(beta);
    if (cosBeta <= kMinCosBeta) return;

    irradianceWPerM2_ = orbit.stellarIrradianceWPerM2;
    periodSeconds_ = orbit.orbitalPeriodS;
    sunCrossTrack_ = std::sin(beta);
    sunPlaneScale_ = cosBeta;

    // The planet's shadow is a cylinder of radius Rp pointing straight away
    // from the star, fixed in the inertial frame. The orbit of radius r cuts
    // it whenever the perpendicular distance from the axis falls under Rp,
    // which reduces to a zenith-cosine threshold on the sun direction.
    const double ratio = core::PlanetRadiusM / orbit.radiusFromPlanetCentreM;
    const double horizon = std::sqrt(std::max(0.0, 1.0 - ratio * ratio));
    const double threshold = horizon / cosBeta;
    if (threshold > 1.0) {
        umbraZenithCosine_ = -2.0; // The shadow is never reached.
        eclipseFraction_ = 0.0;
    } else {
        umbraZenithCosine_ = -threshold;
        eclipseFraction_ = std::acos(threshold) / kPi;
    }
    dark_ = false;
}

StellarExposure StellarExposureEvaluator::evaluate(double elapsedSeconds) const noexcept {
    StellarExposure exposure{};
    if (dark_ || !std::isfinite(elapsedSeconds)) {
        exposure.sunDirection = {0.0, 0.0, 0.0};
        exposure.eclipse = 1.0;
        exposure.sunlit = false;
        return exposure;
    }
    // Fold into [0, 2pi) before evaluating so a long run keeps its phase
    // instead of grinding away precision in a huge argument.
    const double cycles = elapsedSeconds / periodSeconds_;
    const double phase = kTwoPi * (cycles - std::floor(cycles));
    // Local noon is t = 0 with the star at the zenith. The Installation
    // travels along +x, so the star falls behind it: the sweep runs toward
    // -x, and the cross-track component is fixed at sin(beta).
    exposure.sunDirection = {-sunPlaneScale_ * std::sin(phase),
                            sunCrossTrack_,
                            sunPlaneScale_ * std::cos(phase)};
    exposure.eclipse = exposure.sunDirection[2] < umbraZenithCosine_ ? 1.0 : 0.0;
    exposure.sunlit = exposure.eclipse == 0.0;
    return exposure;
}

double StellarExposureEvaluator::directFluxWPerM2(const std::array<double, 3>& outwardNormal,
                                                 double absorptivity,
                                                 const StellarExposure& exposure) const noexcept {
    if (dark_) return 0.0;
    const double incidence = outwardNormal[0] * exposure.sunDirection[0]
        + outwardNormal[1] * exposure.sunDirection[1]
        + outwardNormal[2] * exposure.sunDirection[2];
    // NaN-safe: a surface with no positive incidence receives nothing.
    if (!(incidence > 0.0) || !(absorptivity > 0.0)) return 0.0;
    return incidence * absorptivity * irradianceWPerM2_ * (1.0 - exposure.eclipse);
}

} // namespace nsi::systems
