#include <nsi/systems/StellarExposure.h>

#include <algorithm>
#include <cmath>

namespace nsi::systems {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
// Stefan-Boltzmann in W/(m^2 K^4), the SI form. station-systems carries the
// same constant in kW; this model works in watts throughout.
constexpr double kStefanBoltzmannWPerM2K4 = 5.670374419e-8;
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
        // An Albedo outside [0, 1] would take the energy balance under zero
        // and hand back a NaN planet temperature, so it is refused here as
        // loudly as core refuses it on the way in.
        || !(orbit.albedo >= 0.0) || !(orbit.albedo <= 1.0)
        || !std::isfinite(betaRadians)) return; // dark_ stays true
    const double beta = std::fmod(betaRadians, kTwoPi);
    const double cosBeta = std::cos(beta);
    if (cosBeta <= kMinCosBeta) return;

    irradianceWPerM2_ = orbit.stellarIrradianceWPerM2;
    periodSeconds_ = orbit.orbitalPeriodS;
    albedo_ = orbit.albedo;
    sunCrossTrack_ = std::sin(beta);
    sunPlaneScale_ = cosBeta;
    // The planet's infrared is derived, never typed: whatever stellar light
    // the world does not reflect is absorbed, spread over the whole sphere
    // and re-radiated. The Albedo guard above keeps the radicand
    // non-negative, so the fourth root is never a NaN.
    planetTemperatureK_ = std::pow(irradianceWPerM2_ * (1.0 - albedo_) / (4.0 * kStefanBoltzmannWPerM2K4),
                                  0.25);

    // The planet's shadow is a cylinder of radius Rp pointing straight away
    // from the star, fixed in the inertial frame. The orbit of radius r cuts
    // it whenever the perpendicular distance from the axis falls under Rp,
    // which reduces to a zenith-cosine threshold on the sun direction.
    const double ratio = core::PlanetRadiusM / orbit.radiusFromPlanetCentreM;
    planetViewScale_ = ratio * ratio;
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

double StellarExposureEvaluator::planetViewFactor(
    const std::array<double, 3>& outwardNormal) const noexcept {
    if (dark_) return 0.0;
    // The planet lies straight down the -z axis of the Nadir-lock body frame,
    // so -n . z is the cosine with which the surface faces it. Vertical and
    // upward faces therefore get nothing, which is what a planar element
    // truly sees and is worth knowing before it reads as a bug.
    const double facing = -outwardNormal[2];
    if (!(facing > 0.0)) return 0.0;
    return planetViewScale_ * facing;
}

double StellarExposureEvaluator::planetInfraredFluxWPerM2(
    const std::array<double, 3>& outwardNormal) const noexcept {
    const double view = planetViewFactor(outwardNormal);
    if (view <= 0.0) return 0.0;
    const double t = planetTemperatureK_;
    return view * kStefanBoltzmannWPerM2K4 * t * t * t * t;
}

double StellarExposureEvaluator::reflectedSunlightFluxWPerM2(
    const std::array<double, 3>& outwardNormal, const StellarExposure& exposure) const noexcept {
    // Gated on Eclipse rather than on the sun direction: the whole disc goes
    // dark at once, and a partially lit disc is not modelled.
    if (!exposure.sunlit) return 0.0;
    const double view = planetViewFactor(outwardNormal);
    if (view <= 0.0) return 0.0;
    return view * albedo_ * irradianceWPerM2_;
}

double StellarExposureEvaluator::effectiveSinkTemperatureK(
    double spaceTemperatureK, double viewFactor) const noexcept {
    // No sky means no planet to see, so there is nothing to fold in. The
    // caller's view factor is meaningless here and must not disturb the sink.
    if (dark_ || !(viewFactor > 0.0)) return spaceTemperatureK;
    const double share = viewFactor > 1.0 ? 1.0 : viewFactor;
    const double s = spaceTemperatureK;
    const double p = planetTemperatureK_;
    const double quartic = (1.0 - share) * s * s * s * s + share * p * p * p * p;
    // Both terms are fourth powers, so the sum cannot go negative; the max
    // only guards against a rounding product under zero.
    return std::pow(std::max(0.0, quartic), 0.25);
}

} // namespace nsi::systems
