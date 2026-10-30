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
// (-pi/2, pi/2). Anything at or beyond the orbit pole has no sweep to make
// and is refused as dark. The epsilon is the numerical guard behind that:
// past it the in-plane share of the sweep is too small to divide by.
constexpr double kHalfPi = 1.570796326794896619231846502829916183;
constexpr double kMinCosBeta = 1e-9;
// The Geostationary match tolerance, relative to the planet's sidereal
// rotation. Tight enough that no author can type their way into the badge
// by rounding: a tenth of a second off an Earth-sidereal period is over a
// thousand times the tolerance, while the constant itself survives a
// document round trip bit for bit.
constexpr double kGeostationaryTolerance = 1e-9;
constexpr double kDegreesPerTurn = 360.0;
constexpr double kSecondsPerDay = 86400.0;
// A turn is a whole cycle; folding into [0, 1) keeps every phase argument
// small without ever wrapping a negative into a positive by accident.
inline double fractionalTurns(double turns) noexcept {
    return turns - std::floor(turns);
}
}

StellarExposureEvaluator::StellarExposureEvaluator(
    const core::Installation::Orbit& orbit) noexcept {
    if (!std::isfinite(orbit.stellarIrradianceWPerM2) || orbit.stellarIrradianceWPerM2 <= 0.0
        || !std::isfinite(orbit.orbitalPeriodS) || orbit.orbitalPeriodS <= 0.0
        || !std::isfinite(orbit.radiusFromPlanetCentreM)
        || orbit.radiusFromPlanetCentreM <= core::PlanetRadiusM
        // An Albedo outside [0, 1] would take the energy balance under zero
        // and hand back a NaN planet temperature, so it is refused here as
        // loudly as core refuses it on the way in.
        || !(orbit.albedo >= 0.0) || !(orbit.albedo <= 1.0)
        || !std::isfinite(orbit.phaseOffsetS)) return; // dark_ stays true
    // Beta is refused rather than folded: an angle outside the range core
    // accepts is not a Beta angle, and quietly wrapping it would let the
    // evaluator build a sky from a record the document should never have
    // held. The epsilon guard is the pole itself, where the in-plane share
    // of the sweep vanishes.
    const double beta = orbit.betaAngleRad;
    if (!std::isfinite(beta) || beta <= -kHalfPi || beta >= kHalfPi) return;
    const double cosBeta = std::cos(beta);
    if (cosBeta <= kMinCosBeta) return;

    irradianceWPerM2_ = orbit.stellarIrradianceWPerM2;
    periodSeconds_ = orbit.orbitalPeriodS;
    radiusMeters_ = orbit.radiusFromPlanetCentreM;
    albedo_ = orbit.albedo;
    betaRadians_ = beta;
    phaseOffsetSeconds_ = orbit.phaseOffsetS;
    // The two halves of the sweep, fixed for the whole orbit: the star's
    // cross-track component never moves, and its in-plane share is what
    // rotates at the orbital rate.
    sunCrossTrack_ = std::sin(beta);
    sunPlaneScale_ = cosBeta;
    // Fold the offset into whole turns once, here, so the per-step path
    // adds two numbers of order one instead of carrying a raw second count
    // into a division forever. A negative offset folds forward, which is
    // the same shift travelled the long way round the cycle.
    phaseTurns_ = fractionalTurns(phaseOffsetSeconds_ / periodSeconds_);
    // The planet's infrared is derived, never typed: whatever stellar light
    // the world does not reflect is absorbed, spread over the whole sphere
    // and re-radiated. The Albedo guard above keeps the radicand
    // non-negative, so the fourth root is never a NaN.
    planetTemperatureK_ = std::pow(irradianceWPerM2_ * (1.0 - albedo_) / (4.0 * kStefanBoltzmannWPerM2K4),
                                  0.25);

    // The planet's shadow is a cylinder of radius Rp pointing straight away
    // from the star, fixed in the inertial frame. The orbit of radius r cuts
    // it whenever the perpendicular distance from the axis falls under Rp,
    // which reduces to a fixed zenith-cosine boundary on the sun direction:
    // the station is in shadow exactly when the star sits below
    // -sqrt(1 - (Rp/r)^2). Beta does not move that boundary.
    const double ratio = core::PlanetRadiusM / orbit.radiusFromPlanetCentreM;
    planetViewScale_ = ratio * ratio;
    const double horizon = std::sqrt(std::max(0.0, 1.0 - ratio * ratio));
    umbraZenithCosine_ = -horizon;
    // What Beta moves is how far down the star ever travels - its lowest
    // zenith cosine is -cos(beta) - and therefore how much of the sweep
    // spends time past the boundary. Reading the same geometry off the
    // sweep: the star is under the boundary while
    // cos(phase) < -sqrt(1 - (Rp/r)^2) / cos(beta), which is
    // (1/pi) * acos(that) of the orbit. Once the argument passes one the
    // star never reaches the shadow at all, and the fraction is zero.
    const double sweepArgument = horizon / cosBeta;
    eclipseFraction_ = sweepArgument > 1.0 ? 0.0 : std::acos(sweepArgument) / kPi;
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
    // Fold into [0, 1) before evaluating so a long run keeps its phase
    // instead of grinding away precision in a huge argument, then add the
    // authored offset in the same units. Folding each side separately keeps
    // the elapsed term small whatever the run length; the offset is already
    // folded from the record.
    const double turns = fractionalTurns(fractionalTurns(elapsedSeconds / periodSeconds_)
                                       + phaseTurns_);
    const double phase = kTwoPi * turns;
    // Local noon is t = 0 with the star at the zenith while the offset is
    // zero. The Installation travels along +x, so the star falls behind it:
    // the sweep runs toward -x, and the cross-track component is fixed at
    // sin(beta) all the way round, which is what lets the cross-track hull
    // faces see direct light as Beta grows.
    exposure.sunDirection = {-sunPlaneScale_ * std::sin(phase),
                            sunCrossTrack_,
                            sunPlaneScale_ * std::cos(phase)};
    exposure.eclipse = exposure.sunDirection[2] < umbraZenithCosine_ ? 1.0 : 0.0;
    exposure.sunlit = exposure.eclipse == 0.0;
    return exposure;
}

double StellarExposureEvaluator::groundDriftDegreesPerDay() const noexcept {
    if (dark_) return kUnavailable;
    // Two angular rates over the same axis: the sub-point is carried east by
    // the orbit at 360 degrees per Orbital period, and the surface carries
    // it back west at 360 degrees per sidereal rotation. The residue is
    // the drift an observer on the ground would measure, and it is exactly
    // zero when the two periods match - which is why the badge and this
    // number are shown together. Nothing in the thermal model reads either
    // rate: ADR-0008 puts the planet's rotation outside it entirely.
    return kDegreesPerTurn * kSecondsPerDay
         * (1.0 / periodSeconds_ - 1.0 / core::PlanetSiderealRotationS);
}

bool StellarExposureEvaluator::geostationary() const noexcept {
    if (dark_) return false;
    return std::abs(periodSeconds_ - core::PlanetSiderealRotationS)
         <= kGeostationaryTolerance * core::PlanetSiderealRotationS;
}

double StellarExposureEvaluator::directFluxWPerM2(const std::array<double, 3>& outwardNormal,
                                                 double absorptivity,
                                                 const StellarExposure& exposure) const noexcept {
    if (dark_) return 0.0;
    // The horizon gate. A surface that faces the planet never sees a star
    // that is at or below its own horizon: the planet is exactly what fills
    // that view, so the nadir-facing underside takes no direct sunlight at
    // any Beta and Planet shine is all it has. Vertical normals (n.z = 0)
    // are untouched and keep grazing the low sun right up to the horizon.
    if (outwardNormal[2] < 0.0 && exposure.sunDirection[2] <= 0.0) return 0.0;
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
