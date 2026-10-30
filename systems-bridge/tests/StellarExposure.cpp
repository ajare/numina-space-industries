#include <nsi/systems/CellAtmosphere.h>
#include <nsi/systems/StellarExposure.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

using namespace nsi::systems;
using I = nsi::core::Installation;
using O = I::Orbit;
namespace core = nsi::core;

namespace { bool tracking = false; std::size_t allocations = 0; }
constexpr double kIssPeriod = 5570.0;
constexpr double kIssRadius = 6791000.0;
constexpr double kGeoPeriod = 86164.0;
constexpr double kGeoRadius = 42164000.0;
constexpr double kFullSun = 1361.0;
constexpr double kEarthAlbedo = 0.3;
constexpr double kSigma = 5.670374419e-8;
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kHalfPi = 1.57079632679489661923;
constexpr double kSecondsPerDay = 86400.0;
// The source-slice cap is 0.1 s per whole call, so a long simulated hop has
// to be walked rather than jumped.
constexpr double kMaxHopSeconds = 300.0;
void* operator new(std::size_t n) { if (tracking) ++allocations; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
bool near(double value, double expected, double tolerance) { return std::abs(value - expected) <= tolerance; }
bool nearRel(double value, double expected, double relative) {
    return std::abs(value - expected) <= relative * std::max(1.0, std::abs(expected));
}
void audit(const CellAtmosphereTransport& runtime) {
    require(std::abs(runtime.structure().residualJ) < 1e-6L, "Stellar thermal energy audit failed");
}
// Generous work budgets: this fixture is about the physics, not the
// performance gate, which has its own ticket.
void advance(CellAtmosphereTransport& runtime, double seconds) {
    if (!runtime.advance(seconds, {}, 1u << 22, 1u << 22))
        throw std::runtime_error(runtime.diagnostic());
}
void advanceTo(CellAtmosphereTransport& runtime, double targetSeconds) {
    while (runtime.elapsedSeconds() + kMaxHopSeconds < targetSeconds) advance(runtime, kMaxHopSeconds);
    if (runtime.elapsedSeconds() < targetSeconds) advance(runtime, targetSeconds - runtime.elapsedSeconds());
}
long double solidEnergy(const CellAtmosphereTransport& runtime) {
    long double total = 0;
    for (const auto& body : runtime.structure().bodies) total += body.energyJ();
    return total;
}
bool vectorNear(const std::array<double, 3>& v, std::array<double, 3> e, double tolerance) {
    for (std::size_t i = 0; i < 3; ++i) if (std::abs(v[i] - e[i]) > tolerance) return false;
    return true;
}
double length(const std::array<double, 3>& v) { return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); }
// What the hull actually experiences, counted off the instantaneous rule
// rather than the summary number, so the two can be set against each other.
double sampledEclipseFraction(const StellarExposureEvaluator& evaluator, double period) {
    constexpr int samples = 20000;
    int eclipsed = 0;
    for (int i = 0; i < samples; ++i) {
        const double t = (i + 0.5) * period / samples;
        if (!evaluator.evaluate(t).sunlit) ++eclipsed;
    }
    return static_cast<double>(eclipsed) / samples;
}

// The evaluator is tested through the values it produces, never its internals.
void sunGeometry() {
    const StellarExposureEvaluator iss{O{kFullSun, kIssPeriod, kIssRadius}};
    require(!iss.dark(), "A valid authored Orbit came out dark");
    // Local noon is t = 0 with the star at the zenith.
    require(vectorNear(iss.evaluate(0.0).sunDirection, {0.0, 0.0, 1.0}, 1e-12),
           "t = 0 is not Local noon with the star overhead");
    require(iss.evaluate(0.0).sunlit && iss.evaluate(0.0).eclipse == 0.0, "Local noon must be sunlit");
    // The Installation travels along +x, so the star falls behind it.
    require(vectorNear(iss.evaluate(kIssPeriod / 4.0).sunDirection, {-1.0, 0.0, 0.0}, 1e-9),
           "Quarter orbit must put the star astern along -x");
    require(vectorNear(iss.evaluate(kIssPeriod / 2.0).sunDirection, {0.0, 0.0, -1.0}, 1e-9),
           "Half orbit must put the star at the anti-zenith");
    require(vectorNear(iss.evaluate(3.0 * kIssPeriod / 4.0).sunDirection, {1.0, 0.0, 0.0}, 1e-9),
           "Three-quarter orbit must bring the star ahead along +x");
    // Exactly one sweep per Orbital period, and the frame never drifts.
    for (int cycle = 0; cycle < 4; ++cycle) {
        for (int step = 0; step < 97; ++step) {
            const double t = (cycle + step / 97.0) * kIssPeriod;
            const auto exposure = iss.evaluate(t);
            require(nearRel(length(exposure.sunDirection), 1.0, 1e-12), "Sun direction left the unit sphere");
            // Beta = 0 keeps the sweep inside the x-z plane.
            require(std::abs(exposure.sunDirection[1]) < 1e-12, "Beta = 0 must have no cross-track sun");
            const auto wrapped = iss.evaluate(t - kIssPeriod);
            require(vectorNear(exposure.sunDirection, wrapped.sunDirection, 1e-9),
                   "Sweep is not periodic in the Orbital period");
        }
    }
    // A huge elapsed time folds rather than grinding away the phase.
    require(vectorNear(iss.evaluate(1.0e9 * kIssPeriod).sunDirection, {0.0, 0.0, 1.0}, 1e-6),
           "Long-run phase folded inaccurately");
    // A dark evaluator never invents a star.
    const StellarExposureEvaluator dark{};
    require(dark.dark() && dark.eclipseFraction() == 0.0 && dark.stellarIrradianceWPerM2() == 0.0,
           "Default evaluator must be dark");
    require(length(dark.evaluate(123.0).sunDirection) == 0.0 && !dark.evaluate(123.0).sunlit,
           "Dark evaluator produced a sun");
}

void eclipseFraction() {
    const auto analytic = [](double radius) {
        return std::asin(core::PlanetRadiusM / radius) / 3.14159265358979323846;
    };
    const StellarExposureEvaluator iss{O{kFullSun, kIssPeriod, kIssRadius}};
    const StellarExposureEvaluator geo{O{kFullSun, kGeoPeriod, kGeoRadius}};
    // The ticket's reference numbers, to the project's 5% tolerance convention.
    require(nearRel(iss.eclipseFraction(), 0.387, 0.05), "ISS-like eclipse fraction is not 38.7%");
    require(nearRel(geo.eclipseFraction(), 0.0483, 0.05), "Geostationary eclipse fraction is not 4.83%");
    require(nearRel(iss.eclipseFraction(), analytic(kIssRadius), 1e-12),
           "ISS eclipse fraction departed from asin(Rp/r)/pi");
    require(nearRel(geo.eclipseFraction(), analytic(kGeoRadius), 1e-12),
           "Geostationary eclipse fraction departed from asin(Rp/r)/pi");
    // The instantaneous rule and the orbit-average must agree, or the summary
    // would be a number unconnected to what the hull actually experiences.
    require(std::abs(sampledEclipseFraction(iss, kIssPeriod) - iss.eclipseFraction()) < 2.0 / 20000.0,
           "Instantaneous eclipse disagrees with the orbit-average fraction (ISS)");
    require(std::abs(sampledEclipseFraction(geo, kGeoPeriod) - geo.eclipseFraction()) < 2.0 / 20000.0,
           "Instantaneous eclipse disagrees with the orbit-average fraction (GEO)");
    // Eclipse is centred on local midnight, and local noon is never eclipsed.
    require(iss.evaluate(kIssPeriod / 2.0).eclipse == 1.0, "Local midnight must be eclipsed at Beta = 0");
    require(iss.evaluate(0.0).eclipse == 0.0, "Local noon must never be eclipsed");
    // The shadow cylinder extends forever, so any orbit clear of the surface
    // is grazed a little; a high one barely at all, and monotonically less.
    const StellarExposureEvaluator high{O{kFullSun, 1000.0, 100.0 * core::PlanetRadiusM}};
    require(high.eclipseFraction() < 0.005, "A very high orbit should be barely eclipsed");
    require(high.eclipseFraction() < geo.eclipseFraction() && geo.eclipseFraction() < iss.eclipseFraction(),
           "Eclipse fraction did not fall with orbital radius");
}

// The ticket's own contract written out: the eclipse fraction is
// (1/pi) * acos( sqrt(1 - (Rp/r)^2) / cos(beta) ), and zero once that
// argument exceeds one. The reference table's 69.7 deg and 8.7 deg are
// independent anchors read off the design, not off this implementation.
void eclipseFractionFollowsBeta() {
    const auto analytic = [](double radius, double beta) {
        const double ratio = core::PlanetRadiusM / radius;
        const double argument = std::sqrt(1.0 - ratio * ratio) / std::cos(beta);
        return argument > 1.0 ? 0.0 : std::acos(argument) / kPi;
    };
    double previousIss = analytic(kIssRadius, 0.0) + 1.0;
    double previousGeo = analytic(kGeoRadius, 0.0) + 1.0;
    for (double degrees = 0.0; degrees <= 89.0; degrees += 1.0) {
        const double beta = degrees * kDegToRad;
        const StellarExposureEvaluator iss{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, beta}};
        const StellarExposureEvaluator geo{O{kFullSun, kGeoPeriod, kGeoRadius, kEarthAlbedo, beta}};
        require(!iss.dark() && !geo.dark(), "A legal Beta angle was refused");
        require(nearRel(iss.eclipseFraction(), analytic(kIssRadius, beta), 1e-12),
               "ISS eclipse fraction left (1/pi) acos(sqrt(1-(Rp/r)^2)/cos beta)");
        require(nearRel(geo.eclipseFraction(), analytic(kGeoRadius, beta), 1e-12),
               "Geostationary eclipse fraction left (1/pi) acos(sqrt(1-(Rp/r)^2)/cos beta)");
        // Beta only ever takes eclipse away: never negative, never more than
        // the coplanar worst case, and never rising as Beta climbs.
        require(iss.eclipseFraction() <= analytic(kIssRadius, 0.0) + 1e-12
                   && geo.eclipseFraction() <= analytic(kGeoRadius, 0.0) + 1e-12,
               "Beta increased the eclipse above its coplanar worst case");
        require(iss.eclipseFraction() <= previousIss + 1e-12,
               "ISS eclipse rose as Beta grew");
        require(geo.eclipseFraction() <= previousGeo + 1e-12,
               "Geostationary eclipse rose as Beta grew");
        previousIss = iss.eclipseFraction();
        previousGeo = geo.eclipseFraction();
        // The summary still describes what the hull sees, tilted and all.
        require(std::abs(sampledEclipseFraction(iss, kIssPeriod) - iss.eclipseFraction())
                   < 2.0 / 20000.0,
               "Instantaneous eclipse disagrees with the Beta-tilted average (ISS)");
    }
    // The tabulated critical angles, to the tenth of a degree: the Beta
    // above which each radius is not eclipsed at all.
    const auto criticalDegrees = [](double radius) {
        const double ratio = core::PlanetRadiusM / radius;
        return std::acos(std::sqrt(1.0 - ratio * ratio)) / kDegToRad;
    };
    const double issCritical = criticalDegrees(kIssRadius);
    const double geoCritical = criticalDegrees(kGeoRadius);
    require(std::abs(issCritical - 69.7) <= 0.05,
           "The ISS-like critical Beta is not the tabulated 69.7 deg");
    require(std::abs(geoCritical - 8.7) <= 0.05,
           "The geostationary critical Beta is not the tabulated 8.7 deg");
    // A tenth of a degree either side of each boundary: a shadow below it,
    // none above it.
    const StellarExposureEvaluator issLast{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo,
                                           (issCritical - 0.1) * kDegToRad}};
    const StellarExposureEvaluator issClear{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo,
                                            (issCritical + 0.1) * kDegToRad}};
    require(issLast.eclipseFraction() > 0.0,
           "An ISS-like orbit stopped being eclipsed before its critical Beta");
    require(issClear.eclipseFraction() == 0.0,
           "An ISS-like orbit was still eclipsed above its critical Beta");
    const StellarExposureEvaluator geoLast{O{kFullSun, kGeoPeriod, kGeoRadius, kEarthAlbedo,
                                           (geoCritical - 0.1) * kDegToRad}};
    const StellarExposureEvaluator geoClear{O{kFullSun, kGeoPeriod, kGeoRadius, kEarthAlbedo,
                                            (geoCritical + 0.1) * kDegToRad}};
    require(geoLast.eclipseFraction() > 0.0,
           "A geostationary orbit stopped being eclipsed before its critical Beta");
    require(geoClear.eclipseFraction() == 0.0,
           "A geostationary orbit was still eclipsed above its critical Beta");
    // And the eclipse is really gone above the boundary, not merely small:
    // a whole orbit of sampling finds no shadow at all.
    for (int step = 0; step <= 4000; ++step) {
        const double t = step * kIssPeriod / 4000.0;
        require(issClear.evaluate(t).sunlit, "A shadow survived above the critical Beta (ISS)");
        require(geoClear.evaluate(t).sunlit, "A shadow survived above the critical Beta (GEO)");
    }
    // Continuity at the boundary. Approaching from below, the eclipsed arc
    // shrinks smoothly toward nothing; there is no step to fall off.
    const double betaCritical = std::acos(std::sqrt(1.0
        - std::pow(core::PlanetRadiusM / kIssRadius, 2)));
    double shrinking = 1.0;
    for (double slack = 8e-3; slack >= 1e-7; slack *= 0.5) {
        const StellarExposureEvaluator near{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo,
                                            betaCritical - slack}};
        const double fraction = near.eclipseFraction();
        require(fraction > 0.0, "The eclipse vanished before it reached the boundary");
        require(fraction < shrinking, "The eclipse did not close smoothly toward the boundary");
        require(std::abs(sampledEclipseFraction(near, kIssPeriod) - fraction) < 2.0 / 20000.0,
               "The closing eclipse arc disagrees with its own fraction");
        shrinking = fraction;
    }
    require(shrinking < 1e-3, "The eclipse was still substantial at the boundary");
    // On the boundary itself it is zero, which is where the limit was
    // already heading, and above it stays zero: continuous, not a jump.
    const StellarExposureEvaluator onBoundary{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo,
                                              betaCritical}};
    require(onBoundary.eclipseFraction() < 1e-6, "The eclipse did not close at the boundary");
    require(onBoundary.eclipseFraction() <= shrinking + 1e-6,
           "The eclipse jumped at the boundary instead of closing");
}

// Beta is not only an eclipse knob: it tilts a constant cross-track share
// of the star into the sky, which is the only reason the cross-track hull
// faces ever get direct light at all.
void betaTiltsTheSunAcrossTrack() {
    const double beta = 35.0 * kDegToRad;
    const StellarExposureEvaluator tilted{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, beta}};
    require(!tilted.dark(), "Beta 35 deg was refused");
    require(near(tilted.betaAngleRadians(), beta, 1e-12), "Evaluator dropped the authored Beta");
    for (int step = 0; step <= 997; ++step) {
        const double t = step * kIssPeriod / 997.0;
        const auto s = tilted.evaluate(t).sunDirection;
        // The cross-track component is fixed all the way round the orbit.
        require(near(s[1], std::sin(beta), 1e-12), "The cross-track sun component moved");
        // What is left of the star still sweeps the whole circle in-plane.
        require(nearRel(std::sqrt(s[0] * s[0] + s[2] * s[2]), std::cos(beta), 1e-12),
               "The in-plane sweep lost its cos(beta) share");
        require(nearRel(length(s), 1.0, 1e-12), "Beta put the star off the unit sphere");
    }
    // Local noon is still in the zenith plane, lifted only by Beta.
    require(vectorNear(tilted.evaluate(0.0).sunDirection, {0.0, std::sin(beta), std::cos(beta)}, 1e-12),
           "Local noon did not put the tilted star in the zenith plane");
    // The sweep rate is the Orbital period, unchanged by the tilt.
    require(vectorNear(tilted.evaluate(kIssPeriod).sunDirection,
                     tilted.evaluate(0.0).sunDirection, 1e-9),
           "Beta changed the sweep period");
    require(vectorNear(tilted.evaluate(2.0 * kIssPeriod).sunDirection,
                     tilted.evaluate(0.0).sunDirection, 1e-9),
           "Beta broke the sweep's periodicity");
    // A mirrored Beta mirrors the cross-track component and leaves the
    // eclipse exactly as it was, because the shadow only knows cos(beta).
    const StellarExposureEvaluator mirrored{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, -beta}};
    require(near(mirrored.evaluate(0.0).sunDirection[1], -std::sin(beta), 1e-12),
           "A negative Beta did not mirror the cross-track component");
    require(near(mirrored.eclipseFraction(), tilted.eclipseFraction(), 1e-12),
           "A mirrored Beta changed the eclipse fraction");
}

// The cross-track faces come alive with Beta; the nadir underside never
// gets a share of the direct beam, whatever Beta is doing.
void crossTrackFacesLightUpWithBeta() {
    const double alpha = 0.4;
    const StellarExposureEvaluator coplanar{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    // At Beta = 0 no cross-track normal ever sees the direct beam.
    for (int step = 0; step <= 997; ++step) {
        const double t = step * kIssPeriod / 997.0;
        const auto exposure = coplanar.evaluate(t);
        require(coplanar.directFluxWPerM2({0.0, 1.0, 0.0}, alpha, exposure) == 0.0,
               "A cross-track face caught direct light at Beta = 0");
        require(coplanar.directFluxWPerM2({0.0, -1.0, 0.0}, alpha, exposure) == 0.0,
               "The opposite cross-track face caught direct light at Beta = 0");
    }
    // Growing Beta hands the +y faces an ever-larger share, exactly
    // sin(beta) at Local noon, and monotonically so.
    double previous = -1.0;
    for (double degrees = 0.0; degrees <= 85.0; degrees += 5.0) {
        const double beta = degrees * kDegToRad;
        const StellarExposureEvaluator tilted{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, beta}};
        const double flux = tilted.directFluxWPerM2({0.0, 1.0, 0.0}, alpha, tilted.evaluate(0.0));
        require(nearRel(flux, std::sin(beta) * alpha * kFullSun, 1e-12),
               "Cross-track incidence at Local noon is not sin(beta)");
        require(flux > previous, "Cross-track direct flux did not grow with Beta");
        previous = flux;
    }
    require(previous > 0.99 * alpha * kFullSun,
           "The cross-track faces never got near full sun however far Beta went");
    // The tilt picks a side: +y takes the light for a positive Beta, -y
    // for a negative one, and neither face gets it at Beta = 0.
    const double beta = 35.0 * kDegToRad;
    const StellarExposureEvaluator north{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, beta}};
    const StellarExposureEvaluator south{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, -beta}};
    const auto noon = north.evaluate(0.0);
    const auto mirroredNoon = south.evaluate(0.0);
    require(north.directFluxWPerM2({0.0, 1.0, 0.0}, alpha, noon) > 0.0,
           "A positive Beta left the +y face dark");
    require(north.directFluxWPerM2({0.0, -1.0, 0.0}, alpha, noon) == 0.0,
           "A positive Beta lit the -y face");
    require(south.directFluxWPerM2({0.0, -1.0, 0.0}, alpha, mirroredNoon) > 0.0,
           "A negative Beta left the -y face dark");
    require(south.directFluxWPerM2({0.0, 1.0, 0.0}, alpha, mirroredNoon) == 0.0,
           "A negative Beta lit the +y face");
    require(near(north.directFluxWPerM2({0.0, 1.0, 0.0}, alpha, noon),
                south.directFluxWPerM2({0.0, -1.0, 0.0}, alpha, mirroredNoon), 1e-12),
           "The mirrored cross-track faces took unequal shares");
}

// The nadir-facing underside has Planet shine and nothing else. This is the
// gate that makes that a guarantee rather than an accident of Beta = 0.
void nadirNeverSeesDirectSun() {
    const double alpha = 0.4;
    const std::array<double, 3> nadir{0.0, 0.0, -1.0};
    for (double degrees = 0.0; degrees <= 89.0; degrees += 1.0) {
        const StellarExposureEvaluator tilted{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo,
                                              degrees * kDegToRad}};
        for (int step = 0; step <= 997; ++step) {
            const double t = step * kIssPeriod / 997.0;
            require(tilted.directFluxWPerM2(nadir, alpha, tilted.evaluate(t)) == 0.0,
                   "The nadir normal received direct flux");
        }
        // Non-vacuity: the roof is taking full sun at every one of these
        // Betas, so the nadir reading is a gate and not a dead evaluator.
        require(tilted.directFluxWPerM2({0.0, 0.0, 1.0}, alpha, tilted.evaluate(0.0)) > 0.0,
               "The fixture went dark while testing the nadir gate");
        // And the underside is not left with nothing: Planet shine still
        // arrives, which is the whole point of the guarantee.
        require(tilted.planetInfraredFluxWPerM2(nadir) > 0.0,
               "The nadir gate took Planet shine with it");
    }
    // Where exactly the gate bites. A face leaning down and astern would
    // geometrically catch a star that has dropped below the horizon; the
    // planet is what fills that view, so it is refused one. Lift the same
    // star a hair above the horizon and the same face takes it.
    const StellarExposureEvaluator coplanar{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    const std::array<double, 3> downAndAstern{-0.7071067811865475, 0.0, -0.7071067811865475};
    StellarExposure justAbove{};
    justAbove.sunDirection = {-0.9999999, 0.0, 1.0e-4};
    justAbove.eclipse = 0.0;
    justAbove.sunlit = true;
    StellarExposure justBelow{};
    justBelow.sunDirection = {-0.9999999, 0.0, -1.0e-4};
    justBelow.eclipse = 0.0;
    justBelow.sunlit = true;
    require(coplanar.directFluxWPerM2(downAndAstern, alpha, justAbove) > 0.0,
           "A downward-facing face was refused a star above the horizon");
    require(coplanar.directFluxWPerM2(downAndAstern, alpha, justBelow) == 0.0,
           "A downward-facing face caught a star below the horizon");
    // Vertical normals are not gated: a wall still grazes a low sun right
    // up to the horizon, which is why the gate is on the normal and not on
    // the star alone.
    require(coplanar.directFluxWPerM2({-1.0, 0.0, 0.0}, alpha, justBelow) > 0.0,
           "A vertical face was gated out of a low sun");
    require(coplanar.directFluxWPerM2({-1.0, 0.0, 0.0}, alpha, justAbove) > 0.0,
           "A vertical face lost its sun above the horizon");
}

// The phase offset moves when things happen and nothing else: the whole
// profile translates, the eclipse keeps its size, and t = 0 is still Local
// noon as long as the offset is zero.
void phaseOffsetShiftsTheProfile() {
    const StellarExposureEvaluator reference{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    const double offset = 0.4 * kIssPeriod;
    const StellarExposureEvaluator shifted{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, 0.0, offset}};
    require(!shifted.dark(), "A phase offset was refused");
    require(near(shifted.phaseOffsetSeconds(), offset, 1e-12), "Evaluator dropped the authored phase offset");
    // The whole profile moves: the shifted exposure at t is the unshifted
    // exposure at t + offset, everywhere and in every respect.
    for (int step = 0; step <= 997; ++step) {
        const double t = step * kIssPeriod / 997.0;
        const auto moved = shifted.evaluate(t);
        const auto original = reference.evaluate(t + offset);
        require(vectorNear(moved.sunDirection, original.sunDirection, 1e-9),
               "The phase offset did not shift the sun sweep");
        require(moved.eclipse == original.eclipse, "The phase offset moved the eclipse unevenly");
    }
    // Shape and duration preserved: the fraction is the same number, and
    // the sampled arc under the shifted clock agrees with it.
    require(shifted.eclipseFraction() == reference.eclipseFraction(),
           "The phase offset resized the eclipse");
    require(std::abs(sampledEclipseFraction(shifted, kIssPeriod) - reference.eclipseFraction())
               < 2.0 / 20000.0,
           "The shifted eclipse arc disagrees with the unshifted fraction");
    // t = 0 is Local noon while the offset is zero, and is not once it is
    // not. A half-period offset opens the run mid-eclipse, which is the
    // reason the knob exists.
    require(vectorNear(reference.evaluate(0.0).sunDirection, {0.0, 0.0, 1.0}, 1e-12),
           "Zero offset no longer opens at Local noon");
    require(!shifted.evaluate(0.0).sunlit, "A 0.4-period offset did not open the run inside the eclipse");
    const StellarExposureEvaluator midnight{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, 0.0,
                                            kIssPeriod / 2.0}};
    require(!midnight.evaluate(0.0).sunlit, "A half-period offset did not open the run at local midnight");
    require(vectorNear(midnight.evaluate(kIssPeriod / 2.0).sunDirection, {0.0, 0.0, 1.0}, 1e-9),
           "A half-period offset did not return to Local noon half an orbit later");
    // Offsets fold: whole extra periods change nothing, and a negative
    // offset is the same shift travelled the long way round the cycle.
    const StellarExposureEvaluator wrapped{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, 0.0,
                                           offset + 3.0 * kIssPeriod}};
    const StellarExposureEvaluator backwards{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, 0.0,
                                              -offset}};
    const StellarExposureEvaluator forwardsLong{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, 0.0,
                                                kIssPeriod - offset}};
    for (int step = 0; step <= 997; ++step) {
        const double t = step * kIssPeriod / 997.0;
        require(vectorNear(wrapped.evaluate(t).sunDirection, shifted.evaluate(t).sunDirection, 1e-9),
               "A whole extra period of offset changed the profile");
        require(vectorNear(backwards.evaluate(t).sunDirection, forwardsLong.evaluate(t).sunDirection, 1e-9),
               "A negative offset is not the folded-forward positive shift");
    }
    // A tilted and shifted orbit shifts just the same.
    const double beta = 35.0 * kDegToRad;
    const StellarExposureEvaluator tiltedReference{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, beta}};
    const StellarExposureEvaluator tiltedShifted{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, beta,
                                                 offset}};
    for (int step = 0; step <= 997; ++step) {
        const double t = step * kIssPeriod / 997.0;
        require(vectorNear(tiltedShifted.evaluate(t).sunDirection,
                         tiltedReference.evaluate(t + offset).sunDirection, 1e-9),
               "A tilted orbit did not shift with its phase offset");
    }
    require(tiltedShifted.eclipseFraction() == tiltedReference.eclipseFraction(),
           "A phase offset resized a tilted orbit's eclipse");
}

void directFlux() {
    const double alpha = 0.4;
    const StellarExposureEvaluator iss{O{kFullSun, kIssPeriod, kIssRadius}};
    const auto noon = iss.evaluate(0.0);
    // Zenith face at normal incidence takes everything the absorptivity allows.
    require(nearRel(iss.directFluxWPerM2({0.0, 0.0, 1.0}, alpha, noon), alpha * kFullSun, 1e-12),
           "Zenith face did not take full sun at Local noon");
    // Incidence falls as the cosine, and never below zero.
    const auto raking = iss.evaluate(kIssPeriod * 0.125); // 45 degrees past Local noon
    require(nearRel(iss.directFluxWPerM2({0.0, 0.0, 1.0}, alpha, raking),
                   std::sqrt(0.5) * alpha * kFullSun, 1e-9),
           "Direct flux did not follow the cosine of incidence");
    require(iss.directFluxWPerM2({0.0, 0.0, -1.0}, alpha, noon) == 0.0,
           "A face turned away from the star received flux");
    require(iss.directFluxWPerM2({0.0, 1.0, 0.0}, alpha, noon) == 0.0,
           "Cross-track face received light at Beta = 0");
    require(iss.directFluxWPerM2({0.0, 1.0, 0.0}, alpha, raking) == 0.0,
           "Cross-track face received light at Beta = 0 later in the orbit");
    // Eclipse puts every surface in the dark, whichever way it faces.
    const auto midnight = iss.evaluate(kIssPeriod / 2.0);
    for (const auto& face : std::array<std::array<double, 3>, 6>{{{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}}})
        require(iss.directFluxWPerM2(face, alpha, midnight) == 0.0,
               "Eclipse did not remove direct flux");
    // Zero absorptivity absorbs nothing; nothing is ever negative.
    require(iss.directFluxWPerM2({0.0, 0.0, 1.0}, 0.0, noon) == 0.0, "Zero absorptivity absorbed");
    for (double x = -2.0; x <= 2.0; x += 0.13)
        require(iss.directFluxWPerM2({x, x, 1.0}, alpha, noon) >= 0.0, "Direct flux went negative");
    const StellarExposureEvaluator dark{};
    require(dark.directFluxWPerM2({0.0, 0.0, 1.0}, 1.0, noon) == 0.0, "Dark evaluator emitted flux");
}

void planetInfraredIsDerived() {
    // The planet's infrared temperature is never authored: it is whatever the
    // energy balance leaves behind. A dimmer star therefore cools the planet
    // on its own instead of leaving a glowing world orbiting a dead one.
    const auto balance = [](double irradiance, double albedo) {
        return std::pow(irradiance * (1.0 - albedo) / (4.0 * kSigma), 0.25);
    };
    const StellarExposureEvaluator iss{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    require(nearRel(iss.planetInfraredTemperatureK(), balance(kFullSun, kEarthAlbedo), 1e-12),
           "Planet infrared temperature departed from irradiance * (1 - albedo) / 4 sigma");
    require(nearRel(iss.planetInfraredTemperatureK(), 254.6, 0.005),
           "An Earth-like world at full Sun is not the ~255 K it should be");
    require(iss.albedo() == kEarthAlbedo, "Evaluator dropped the authored Albedo");
    // Halving the star cools the planet by the fourth root of a half.
    const StellarExposureEvaluator dim{O{kFullSun / 2.0, kIssPeriod, kIssRadius, kEarthAlbedo}};
    require(nearRel(dim.planetInfraredTemperatureK(), balance(kFullSun / 2.0, kEarthAlbedo), 1e-12),
           "A dimmer star did not cool the planet");
    require(dim.planetInfraredTemperatureK() < iss.planetInfraredTemperatureK(),
           "A dimmer star left the planet as hot as before");
    require(nearRel(dim.planetInfraredTemperatureK(),
                   iss.planetInfraredTemperatureK() * std::pow(0.5, 0.25), 1e-9),
           "Planet temperature did not track the fourth root of irradiance");
    // The two ends of the Albedo range: a black world radiates everything it
    // catches, a perfect mirror keeps none of it and glows with nothing.
    const StellarExposureEvaluator black{O{kFullSun, kIssPeriod, kIssRadius, 0.0}};
    const StellarExposureEvaluator mirror{O{kFullSun, kIssPeriod, kIssRadius, 1.0}};
    require(nearRel(black.planetInfraredTemperatureK(), balance(kFullSun, 0.0), 1e-12),
           "Albedo 0 broke the energy balance");
    require(black.planetInfraredTemperatureK() > 278.0 && black.planetInfraredTemperatureK() < 279.0,
           "A black world at full Sun is not ~278 K");
    require(mirror.planetInfraredTemperatureK() == 0.0,
           "A perfect mirror still found heat to radiate");
    require(black.planetInfraredFluxWPerM2({0.0, 0.0, -1.0})
               > mirror.planetInfraredFluxWPerM2({0.0, 0.0, -1.0}),
           "A black world did not out-glow a mirror");
}

void planetViewFactorIsElementToSphere() {
    const StellarExposureEvaluator iss{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    const StellarExposureEvaluator geo{O{kFullSun, kGeoPeriod, kGeoRadius, kEarthAlbedo}};
    const double issScale = core::PlanetRadiusM / kIssRadius;
    const double geoScale = core::PlanetRadiusM / kGeoRadius;
    // The planar-element-to-sphere result, (Rp/r)^2 * max(0, -n . z).
    require(nearRel(iss.planetViewFactor({0.0, 0.0, -1.0}), issScale * issScale, 1e-12),
           "Nadir view factor is not (Rp/r)^2");
    require(nearRel(iss.planetViewFactor({0.0, 0.0, -1.0}), 0.880, 0.005),
           "ISS nadir view factor is not the tabulated 0.880");
    require(nearRel(geo.planetViewFactor({0.0, 0.0, -1.0}), geoScale * geoScale, 1e-12),
           "Geostationary nadir view factor is not (Rp/r)^2");
    require(nearRel(geo.planetViewFactor({0.0, 0.0, -1.0}), 0.0228, 0.005),
           "Geostationary nadir view factor is not the tabulated 0.0228");
    // Up and sideways see no planet at all from a flat element.
    require(iss.planetViewFactor({0.0, 0.0, 1.0}) == 0.0, "A zenith face saw the planet");
    for (const auto& side : std::array<std::array<double, 3>, 4>{{{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}}})
        require(iss.planetViewFactor(side) == 0.0, "A vertical face saw the planet");
    // Oblique faces scale with the cosine of the angle from nadir.
    const double half = 1.0 / std::sqrt(2.0);
    require(nearRel(iss.planetViewFactor({half, 0.0, -half}), issScale * issScale * half, 1e-12),
           "An oblique nadir-leaning face did not scale by the cosine");
    require(iss.planetViewFactor({half, 0.0, half}) == 0.0, "An upward-leaning face saw the planet");
    // It falls with altitude, and never exceeds unity for a unit normal.
    require(geo.planetViewFactor({0.0, 0.0, -1.0}) < iss.planetViewFactor({0.0, 0.0, -1.0}),
           "View factor did not fall with orbital radius");
    for (double z = -1.0; z <= 1.0001; z += 0.05) {
        const double horizontal = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double clamped = std::max(0.0, std::min(1.0, -z));
        // 0.6^2 + 0.8^2 = 1, so this is a unit normal for every z.
        const double v = iss.planetViewFactor({horizontal * 0.6, horizontal * 0.8, z});
        require(v >= 0.0 && v <= 1.0 + 1e-12, "View factor left [0, 1] for a unit normal");
        require(nearRel(v, issScale * issScale * clamped, 1e-12),
               "View factor did not follow (Rp/r)^2 * max(0, -n . z) over the sphere");
    }
    // And it is emphatically not the sky-coverage fraction 1 - cos(alpha),
    // which is ~35% smaller at this altitude and would understate the load.
    const double alpha = std::asin(issScale);
    const double skyCoverage = 1.0 - std::cos(alpha);
    require(iss.planetViewFactor({0.0, 0.0, -1.0}) > 1.3 * skyCoverage,
           "Nadir view factor looks like the sky-coverage fraction, not element-to-sphere");
}

void planetShineMatchesTheReferenceTable() {
    const StellarExposureEvaluator iss{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    const StellarExposureEvaluator geo{O{kFullSun, kGeoPeriod, kGeoRadius, kEarthAlbedo}};
    const auto noon = iss.evaluate(0.0);
    const auto geoNoon = geo.evaluate(0.0);
    const std::array<double, 3> nadir{0.0, 0.0, -1.0};
    // The ticket's reference numbers for a nadir-facing Structural surface.
    require(nearRel(iss.planetInfraredFluxWPerM2(nadir), 210.0, 0.01),
           "ISS nadir planetary infrared is not the tabulated ~210 W/m^2");
    require(nearRel(iss.reflectedSunlightFluxWPerM2(nadir, noon), 359.0, 0.01),
           "ISS nadir reflected sunlight is not the tabulated ~359 W/m^2");
    require(nearRel(geo.planetInfraredFluxWPerM2(nadir), 5.5, 0.02),
           "Geostationary nadir planetary infrared is not the tabulated ~5.5 W/m^2");
    require(nearRel(geo.reflectedSunlightFluxWPerM2(nadir, geoNoon), 9.3, 0.02),
           "Geostationary nadir reflected sunlight is not the tabulated ~9.3 W/m^2");
    // Together, Planet shine in LEO is about 42% of full sun; in GEO it is
    // noise. The two regimes are not interchangeable.
    const double issShine = iss.planetInfraredFluxWPerM2(nadir) + iss.reflectedSunlightFluxWPerM2(nadir, noon);
    const double geoShine = geo.planetInfraredFluxWPerM2(nadir) + geo.reflectedSunlightFluxWPerM2(nadir, geoNoon);
    require(nearRel(issShine / kFullSun, 0.42, 0.02), "ISS Planet shine is not ~42% of full sun");
    require(geoShine < 20.0, "Geostationary Planet shine is not the negligible figure it should be");
    require(issShine > 20.0 * geoShine, "LEO and GEO Planet shine came out comparable");
    // A face that sees no planet receives neither component.
    for (const auto& normal : std::array<std::array<double, 3>, 3>{{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}}) {
        require(iss.planetInfraredFluxWPerM2(normal) == 0.0, "A planet-blind face received infrared");
        require(iss.reflectedSunlightFluxWPerM2(normal, noon) == 0.0, "A planet-blind face received reflection");
    }
}

void reflectedDiesInEclipseWhileInfraredContinues() {
    const StellarExposureEvaluator iss{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    const std::array<double, 3> nadir{0.0, 0.0, -1.0};
    const auto noon = iss.evaluate(0.0);
    const auto midnight = iss.evaluate(kIssPeriod / 2.0);
    require(midnight.eclipse == 1.0 && !midnight.sunlit, "Mid-eclipse fixture is not eclipsed");
    // Exactly zero, not merely small: there is no sunlight in the shadow.
    require(iss.reflectedSunlightFluxWPerM2(nadir, midnight) == 0.0,
           "Reflected sunlight survived inside the planet's shadow");
    // And the infrared carries on untouched, because the ground beneath an
    // eclipsed station is the planet's own shadowed side, still warm.
    const double infrared = iss.planetInfraredFluxWPerM2(nadir);
    require(infrared > 200.0, "Infrared fixture is too weak to prove anything");
    // Across a whole orbit: the reflected term is zero on exactly the
    // eclipsed set, and the infrared never moves.
    for (int step = 0; step <= 997; ++step) {
        const double t = step * kIssPeriod / 997.0;
        const auto exposure = iss.evaluate(t);
        const double reflected = iss.reflectedSunlightFluxWPerM2(nadir, exposure);
        require(reflected == 0.0 ? !exposure.sunlit : exposure.sunlit,
               "Reflected sunlight and sunlit disagreed");
        if (exposure.sunlit) require(reflected > 0.0, "A sunlit nadir face received no reflection");
        require(iss.planetInfraredFluxWPerM2(nadir) == infrared,
               "Planetary infrared changed through the orbit");
    }
    // A dark sky gives neither.
    const StellarExposureEvaluator dark{};
    require(dark.planetInfraredFluxWPerM2(nadir) == 0.0 && dark.albedo() == 0.0,
           "A dark sky still had a planet");
    require(dark.reflectedSunlightFluxWPerM2(nadir, noon) == 0.0, "A dark sky reflected sunlight");
}

void effectiveSinkSeesThePlanet() {
    const StellarExposureEvaluator iss{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    const double space = 3.0;
    const double nadir = iss.planetViewFactor({0.0, 0.0, -1.0});
    // The stated fold: T_eff^4 is the view-weighted mean of the two sinks.
    const double expected = std::pow((1.0 - nadir) * std::pow(space, 4) + nadir
        * std::pow(iss.planetInfraredTemperatureK(), 4), 0.25);
    require(nearRel(iss.effectiveSinkTemperatureK(space, nadir), expected, 1e-12),
           "Effective sink departed from [(1-F) T_space^4 + F T_planet^4]^(1/4)");
    // A nadir surface radiates toward a warm world, not toward 3 K. This is
    // the whole reason Eclipse stops being a freeze-to-death event.
    require(nearRel(iss.effectiveSinkTemperatureK(space, nadir), 246.6, 0.005),
           "ISS nadir effective sink is not the ~247 K the design promises");
    // No view of the planet means the Deep-space sink is exactly what it was.
    require(iss.effectiveSinkTemperatureK(space, 0.0) == space, "A zero view factor disturbed the sink");
    require(iss.effectiveSinkTemperatureK(3.15, 0.0) == 3.15, "A zero view factor disturbed the sink");
    // A partial view lands between the two, monotonically.
    double previous = space;
    for (double f = 0.0; f <= 1.0001; f += 0.05) {
        const double sink = iss.effectiveSinkTemperatureK(space, f);
        require(sink >= previous - 1e-9, "Effective sink did not rise with the planet's share of the view");
        require(sink >= space - 1e-9 && sink <= iss.planetInfraredTemperatureK() + 1e-9,
               "Effective sink escaped the range spanned by the two sinks");
        previous = sink;
    }
    require(iss.effectiveSinkTemperatureK(space, 1.0)
               > iss.effectiveSinkTemperatureK(space, nadir),
           "A full-planet view was no warmer than a partial one");
    // A dark sky never warms the sink above what it was handed.
    const StellarExposureEvaluator dark{};
    require(dark.effectiveSinkTemperatureK(space, 0.88) == space,
           "A dark sky warmed the Deep-space sink");
}

void refusedInputs() {
    // The evaluator never invents a sky out of numbers that cannot make one.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const std::array<O, 5> bad{
        O{nan, 5570.0, kIssRadius}, O{1361.0, 0.0, kIssRadius},
        O{1361.0, 5570.0, core::PlanetRadiusM}, O{-1.0, 5570.0, kIssRadius},
        O{1361.0, 5570.0, 1.0}};
    for (const auto& orbit : bad) {
        const StellarExposureEvaluator evaluator{orbit};
        require(evaluator.dark(), "Evaluator built a sky from invalid authored numbers");
        const auto exposure = evaluator.evaluate(10.0);
        require(exposure.sunDirection == std::array<double, 3>{0.0, 0.0, 0.0} && !exposure.sunlit,
               "Invalid evaluator produced a sun");
        require(evaluator.directFluxWPerM2({0.0, 0.0, 1.0}, 1.0, exposure) == 0.0,
               "Invalid evaluator emitted flux");
    }
    // A Beta beyond the orbit pole is refused rather than swept, and the
    // pole itself with it: there is no in-plane sweep left to make.
    for (const double beta : {1.6, kHalfPi, -kHalfPi, -1.6, 3.0, nan, inf}) {
        const StellarExposureEvaluator tilted{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, beta}};
        require(tilted.dark(), "A Beta beyond the orbit pole was not refused");
        require(tilted.evaluate(10.0).sunDirection == std::array<double, 3>{0.0, 0.0, 0.0}
                   && !tilted.evaluate(10.0).sunlit,
               "A refused Beta still produced a sun");
        require(tilted.eclipseFraction() == 0.0, "A refused Beta still reported an eclipse fraction");
    }
    // A non-finite phase offset is refused the same way; every finite one,
    // however absurd, is only ever a shift and folds cleanly.
    for (const double offset : {nan, inf, -inf}) {
        const StellarExposureEvaluator shifted{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, 0.0,
                                               offset}};
        require(shifted.dark(), "A non-finite phase offset was not refused");
    }
    const StellarExposureEvaluator huge{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, 0.0,
                                         1.0e12}};
    const StellarExposureEvaluator plain{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    require(!huge.dark() && std::isfinite(huge.evaluate(1234.5).sunDirection[0]),
            "A huge finite phase offset did not fold");
    require(huge.eclipseFraction() == plain.eclipseFraction(),
            "A huge phase offset changed the eclipse fraction");
}

CellAtmosphereExpansion litGeometry(double irradiance = kFullSun, double albedo = kEarthAlbedo) {
    I scene(2, 2);
    scene.setHullType(I::HullType::BareSkin);
    scene.setOrbit(O{irradiance, kIssPeriod, kIssRadius, albedo});
    return expandCellAtmosphere(scene);
}

// The same fixture under a tilted, shifted sky: Beta off the coplanar
// worst case and the clock opened part-way through the eclipse.
CellAtmosphereExpansion tiltedGeometry(double betaDegrees, double phaseOffsetSeconds) {
    I scene(2, 2);
    scene.setHullType(I::HullType::BareSkin);
    scene.setOrbit(O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo,
                    betaDegrees * kDegToRad, phaseOffsetSeconds});
    return expandCellAtmosphere(scene);
}

void expansionCarriesTheOrbit() {
    I scene(1, 1);
    scene.setHullType(I::HullType::BareSkin);
    // A new Installation authors the ISS-like default, so the expansion
    // carries it. Only an explicitly cleared orbit produces no orbit.
    require(expandThermal(scene).orbit==scene.orbit(),
            "Bridge did not carry the authored default Orbit");
    scene.setOrbit(std::nullopt);
    require(!expandThermal(scene).orbit, "Unauthored orbit must not appear in the expansion");
    const O orbit{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo};
    scene.setOrbit(orbit);
    const auto thermal = expandThermal(scene);
    require(thermal.orbit == orbit, "Bridge did not expose the authored Orbit");
    // Beta and the phase offset ride along with the rest of the record.
    const O tilted{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, 0.6, 1234.0};
    scene.setOrbit(tilted);
    require(expandThermal(scene).orbit == tilted,
            "The bridge dropped the authored Beta or phase offset");
    // The Hull's own full-sun field stays for the standalone compartment
    // runtime; the Orbit only takes precedence on the native path.
    require(thermal.hull && thermal.hull->fullSunIrradianceKWPerM2 == 1.361,
           "Authored Hull full-sun irradiance was disturbed");
    const auto expansion = litGeometry(500.0);
    require(expansion.thermal.orbit && expansion.thermal.orbit->stellarIrradianceWPerM2 == 500.0,
           "Cell expansion dropped the authored orbit");
}

double litArea(const CellAtmosphereTransport& runtime, const StellarExposure& exposure) {
    double lit = 0.0, total = 0.0;
    for (const auto& face : runtime.exteriorSolarFaces()) {
        total += face.areaM2;
        if (face.directW(runtime.exposureEvaluator(), exposure) > 0.0) lit += face.areaM2;
    }
    require(total > 0.0, "Fixture has no exterior surface");
    return lit / total;
}

void atMostHalfTheHullIsLit() {
    CellAtmosphereTransport runtime(litGeometry());
    require(!runtime.exteriorSolarFaces().empty(), "No exterior solar faces were precomputed");
    // The invariant itself, asserted directly against the Reset-time normals.
    for (int step = 0; step <= 400; ++step) {
        const double t = step * kIssPeriod / 400.0;
        const auto fraction = litArea(runtime, runtime.exposureEvaluator().evaluate(t));
        require(fraction <= 0.5 + 1e-9, "More than half the hull was in direct sunlight");
    }
    // Non-vacuity: Local noon lights the roof, and the anti-zenith stays dark.
    const auto noon = runtime.exposureEvaluator().evaluate(0.0);
    double zenith = 0.0, total = 0.0;
    for (const auto& face : runtime.exteriorSolarFaces()) {
        total += face.areaM2;
        if (face.outwardNormal[2] > 0.0) zenith += face.areaM2;
    }
    require(zenith > 0.0 && zenith < total, "Fixture has no distinct zenith-facing hull");
    require(nearRel(litArea(runtime, noon), zenith / total, 1e-12),
           "Local noon did not light exactly the upward-facing hull");
    require(litArea(runtime, runtime.exposureEvaluator().evaluate(kIssPeriod / 2.0)) == 0.0,
           "Eclipse left part of the hull lit");
}

// The authored Beta and phase offset reach the transport and change what
// the hull is under, without disturbing the ledger, the audit or replay.
void betaAndPhaseReachTheTransport() {
    const double offset = 0.4 * kIssPeriod;
    CellAtmosphereTransport runtime(tiltedGeometry(35.0, offset));
    const auto& sky = runtime.exposureEvaluator();
    require(!sky.dark() && near(sky.betaAngleRadians(), 35.0 * kDegToRad, 1e-12)
               && near(sky.phaseOffsetSeconds(), offset, 1e-9),
           "The transport did not carry the authored Beta and phase offset");
    // The run opens part-way through an eclipse rather than at Local noon.
    require(!sky.evaluate(0.0).sunlit, "The authored offset did not open the run inside the eclipse");
    require(sky.evaluate(0.4 * kIssPeriod).sunlit, "The shifted day did not arrive when it should");
    const auto crossTrackDirect = [](CellAtmosphereTransport& rt, double t) {
        const auto exposure = rt.exposureEvaluator().evaluate(t);
        double total = 0.0;
        for (const auto& face : rt.exteriorSolarFaces())
            if (face.outwardNormal[1] > 0.5) total += face.directW(rt.exposureEvaluator(), exposure);
        return total;
    };
    const auto nadirDirect = [](CellAtmosphereTransport& rt, double t) {
        const auto exposure = rt.exposureEvaluator().evaluate(t);
        double total = 0.0;
        for (const auto& face : rt.exteriorSolarFaces())
            if (face.outwardNormal[2] < -0.5) total += face.directW(rt.exposureEvaluator(), exposure);
        return total;
    };
    CellAtmosphereTransport flat(litGeometry());
    require(crossTrackDirect(runtime, 0.4 * kIssPeriod) > 0.0,
           "Cross-track hull took no direct sun at Beta = 35 deg");
    require(crossTrackDirect(flat, 0.4 * kIssPeriod) == 0.0,
           "Cross-track hull took direct sun at Beta = 0");
    // The underside stays dark all the way round the tilted orbit, while
    // the roof keeps taking sun: the gate is real, not a dark fixture.
    for (int step = 0; step <= 400; ++step) {
        const double t = step * kIssPeriod / 400.0;
        require(nadirDirect(runtime, t) == 0.0, "The nadir hull took direct stellar power");
    }
    require(crossTrackDirect(runtime, 0.4 * kIssPeriod) > 0.0, "Non-vacuity of the tilted cross-track hull");
    // At most half the hull is lit under the tilt, exactly as under Beta 0.
    for (int step = 0; step <= 400; ++step) {
        const double t = step * kIssPeriod / 400.0;
        const auto fraction = litArea(runtime, runtime.exposureEvaluator().evaluate(t));
        require(fraction <= 0.5 + 1e-9, "More than half the hull was lit at Beta = 35 deg");
    }
    // Walk out of the eclipse the run opened inside and into the shifted
    // daylight, where the tilted hull is actually taking starlight.
    advanceTo(runtime, 0.4 * kIssPeriod);
    advance(runtime, 60.0);
    audit(runtime);
    require(runtime.structure().paths.absorbedSolarJ > 0.0L,
           "A tilted, shifted orbit delivered no stellar energy");
    // The tilted, shifted sky is still allocation-free on the successful
    // path: carrying Beta and the offset adds no heap work per step.
    CellAtmosphereTransport probe(tiltedGeometry(35.0, offset));
    require(probe.advance(0.1), "Tilted warm-up step refused");
    tracking = true;
    const bool advanced = probe.advance(0.1);
    tracking = false;
    require(advanced && allocations == 0, "A tilted, shifted orbital step allocated heap memory");
    audit(probe);
    // Replay is still exact under the new authored numbers.
    CellAtmosphereTransport a(tiltedGeometry(35.0, offset));
    CellAtmosphereTransport b(tiltedGeometry(35.0, offset));
    for (int i = 0; i < 20; ++i) {
        advance(a, 0.1);
        advance(b, 0.1);
    }
    require(a.structure().paths == b.structure().paths,
           "Replay diverged under a tilted, shifted orbit");
    for (std::size_t i = 0; i < a.structure().bodies.size(); ++i)
        require(a.structure().bodies[i].energyJ() == b.structure().bodies[i].energyJ(),
               "Body energies diverged under a tilted, shifted orbit");
    audit(a);
    audit(b);
}

void stellarEnergyReachesTheLedger() {
    CellAtmosphereTransport lit(litGeometry());
    double startedAt = 0.0;
    for (const auto& body : lit.structure().bodies) startedAt = std::max(startedAt, body.temperatureK());
    advance(lit, 60.0);
    audit(lit);
    require(lit.structure().paths.absorbedSolarJ > 0.0L, "Stellar energy did not enter the structural ledger");
    require(lit.structure().rates.absorbedSolarW > 0.0L, "Absorbed stellar rate was not published");
    // The sun-facing hull warmed above where it started while the shaded hull
    // carried on cooling: sunlight is applied per surface, not as a global
    // dimmer that would move every face together.
    double litWarmest = -1e300, shadedCoolest = 1e300;
    for (const auto& face : lit.exteriorSolarFaces()) {
        const auto temperature = lit.structure().bodies[face.body].temperatureK();
        if (face.outwardNormal[2] > 0.0) litWarmest = std::max(litWarmest, temperature);
        else shadedCoolest = std::min(shadedCoolest, temperature);
    }
    require(litWarmest > startedAt, "The sun-facing hull did not warm");
    require(shadedCoolest < startedAt, "The shaded hull did not cool");
    require(litWarmest - shadedCoolest > 1.0, "Lit and shaded hull faces barely diverged");
    // Counted exactly once: the exterior net heat is the stellar input less
    // what the hull radiated and leaked, with no second stellar term hiding
    // inside the sink.
    const auto& p = lit.structure().paths;
    require(std::abs(lit.structure().exteriorHeatJ
        - (p.absorbedSolarJ - p.exteriorRadiationJ - p.portalEscapeJ)) < 1e-9L,
        "Stellar input was not counted exactly once in the exterior heat");

    // The same Installation under no orbit stays cold by comparison.
    I dark(2, 2);
    dark.setHullType(I::HullType::BareSkin);
    // The contrast needs a genuinely dark control: a fresh Installation now
    // authors a sky, so the orbit has to be cleared to get one.
    dark.setOrbit(std::nullopt);
    CellAtmosphereTransport darkRuntime(dark);
    advance(darkRuntime, 60.0);
    audit(darkRuntime);
    require(darkRuntime.structure().paths.absorbedSolarJ == 0.0L, "Unauthored orbit still absorbed starlight");
    require(solidEnergy(lit) > solidEnergy(darkRuntime), "A lit hull did not out-warm a dark one");
}

void theOrbitSetsTheIrradiance() {
    // The authored Orbit number, not the Hull's legacy full-sun field, drives
    // the native path: doubling it doubles the absorbed power.
    auto absorbed = [](double irradiance) {
        CellAtmosphereTransport runtime(litGeometry(irradiance));
        advance(runtime, 0.1);
        return runtime.structure().rates.absorbedSolarW;
    };
    const auto half = absorbed(500.0);
    const auto whole = absorbed(1000.0);
    require(half > 0.0L && whole > 0.0L, "Orbital irradiance produced no absorbed power");
    require(nearRel(double(whole), 2.0 * double(half), 1e-9),
           "Absorbed power did not scale with the authored Stellar irradiance");
    // And it matches the per-face sum the Reset-time normals imply: every
    // watt of absorbed solar, the direct beam and the planet's reflection
    // alike, arrives through exactly one face exactly once.
    CellAtmosphereTransport runtime(litGeometry(kFullSun));
    advance(runtime, 0.1);
    double direct = 0.0, reflected = 0.0;
    // The step midpoint sits inside (0, 0.1); the sweep moves ~1e-9 over it.
    const auto noon = runtime.exposureEvaluator().evaluate(0.05);
    for (const auto& face : runtime.exteriorSolarFaces()) {
        direct += face.directW(runtime.exposureEvaluator(), noon);
        reflected += face.reflectedSunlightW(runtime.exposureEvaluator(), noon);
    }
    require(reflected > 0.0, "A sunlit hull received no reflected sunlight");
    require(nearRel(double(runtime.structure().rates.absorbedSolarW), direct + reflected, 1e-6),
           "Whole-call absorbed power did not match the per-face direct plus reflected flux");
    // The reflection is a real share of the load, not a rounding term.
    require(reflected > 0.1 * (direct + reflected),
           "Reflected sunlight was a negligible share of absorbed solar");
}

double faceTemperature(const CellAtmosphereTransport& runtime, int sign) {
    double best = -1e300;
    for (const auto& face : runtime.exteriorSolarFaces()) {
        if (sign > 0 ? face.outwardNormal[2] > 0.0 : face.outwardNormal[2] < 0.0)
            best = std::max(best, runtime.structure().bodies[face.body].temperatureK());
    }
    require(best > -1e299, "Fixture has no exterior face of the requested orientation");
    return best;
}

double planetShineArriving(const CellAtmosphereTransport& runtime, const StellarExposure& exposure) {
    double total = 0.0;
    for (const auto& face : runtime.exteriorSolarFaces())
        total += face.planetInfraredW(runtime.exposureEvaluator())
            + face.reflectedSunlightW(runtime.exposureEvaluator(), exposure);
    return total;
}

struct EclipseTrough {
    double nadirEntryK{};
    double nadirMidK{};
    double zenithMidK{};
    double coolDownK{};
};

EclipseTrough measureEclipse(double albedo) {
    const StellarExposureEvaluator reference{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    const double eclipseEntry = kIssPeriod * (1.0 - reference.eclipseFraction()) / 2.0;
    CellAtmosphereTransport runtime(litGeometry(kFullSun, albedo));
    advanceTo(runtime, eclipseEntry);
    EclipseTrough trough;
    trough.nadirEntryK = faceTemperature(runtime, -1);
    advanceTo(runtime, kIssPeriod / 2.0);
    trough.nadirMidK = faceTemperature(runtime, -1);
    trough.zenithMidK = faceTemperature(runtime, +1);
    trough.coolDownK = trough.nadirEntryK - trough.nadirMidK;
    require(trough.coolDownK > 0.0, "The hull warmed through the eclipse");
    audit(runtime);
    return trough;
}

// An Albedo of 1 leaves the planet's energy balance with nothing left to
// radiate, so through the Eclipse the nadir face sees the bare Deep-space
// sink: the pre-feature behaviour, with the orbit, the eclipse geometry and
// the direct beam all left exactly as they were. That makes it a clean
// control for what Planet shine actually buys.
void planetShineSoftensTheEclipseTrough() {
    const auto shine = measureEclipse(kEarthAlbedo);
    const auto control = measureEclipse(1.0);
    // The eclipse cool-down is materially shallower with a warm world below.
    require(shine.coolDownK < 0.75 * control.coolDownK,
           "Planet shine did not materially shallow the eclipse cool-down");
    require(shine.nadirMidK > control.nadirMidK + 15.0,
           "The eclipsed nadir face was no warmer with Planet shine than without");
    // Within one run the two faces are in the same shadow at the same
    // instant; the only thing that separates them is whether the planet is
    // in their view. With Planet shine the nadir face is the warmer one.
    require(shine.nadirMidK - shine.zenithMidK > 10.0,
           "A planet-facing surface did not out-warm a sky-facing one through eclipse");
    // Take the planet's infrared away and the asymmetry goes with it, which
    // is what makes the gap above a Planet-shine reading rather than a
    // property of the fixture.
    require(std::abs(control.nadirMidK - control.zenithMidK) < 0.5,
           "A planet-blind eclipse still showed a nadir/zenith asymmetry");
}

void planetaryInfraredIsNotSolarAndTheAuditCloses() {
    const StellarExposureEvaluator reference{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    const double eclipseEntry = kIssPeriod * (1.0 - reference.eclipseFraction()) / 2.0;
    CellAtmosphereTransport runtime(litGeometry());
    advanceTo(runtime, eclipseEntry + 10.0);
    audit(runtime);
    const auto solarBefore = runtime.structure().paths.absorbedSolarJ;
    advance(runtime, 20.0);
    const auto exposure = runtime.exposureEvaluator().evaluate(eclipseEntry + 20.0);
    require(!exposure.sunlit, "The eclipse step was not inside the shadow");
    // Planet shine is arriving in quantity, all of it infrared.
    const double arriving = planetShineArriving(runtime, exposure);
    require(arriving > 1000.0, "No planetary infrared was arriving during eclipse");
    // None of it is booked as absorbed solar. It went into the sink instead,
    // where a grey body takes it with the very emissivity it emits with,
    // which is why the solar term stays exactly where it was.
    require(runtime.structure().paths.absorbedSolarJ == solarBefore,
           "Planetary infrared was counted as absorbed solar");
    require(runtime.structure().rates.absorbedSolarW == 0.0L,
           "An eclipsed step published an absorbed solar rate");
    // The audit still closes: the new contribution sits inside the exterior
    // radiation term once, and every internal transfer still cancels.
    audit(runtime);
    const auto& paths = runtime.structure().paths;
    require(std::abs(runtime.structure().exteriorHeatJ
        - (paths.absorbedSolarJ - paths.exteriorRadiationJ - paths.portalEscapeJ)) < 1e-9L,
        "The planet-folded sink did not close the exterior heat audit");
    // And the sink is still a sink: the eclipsed hull is losing heat through
    // it, just a great deal less of it than a hull over a dead sky would.
    require(runtime.structure().rates.exteriorRadiationW > 0.0L,
           "The eclipsed hull was not losing heat to the sink at all");
}

void noOrbitKeepsTheSkyDark() {
    I scene(2, 2);
    scene.setHullType(I::HullType::BareSkin);
    // The dark-sky guarantee, re-homed: a new Installation now authors a
    // sky by default, so the guarantee is asserted against an explicitly
    // cleared orbit rather than against a fresh document.
    scene.setOrbit(std::nullopt);
    CellAtmosphereTransport runtime(scene);
    require(runtime.exposureEvaluator().dark(), "Transport built a sky with no authored Orbit");
    require(!runtime.solarCycleOverrides(), "A skyless transport reported a profile override");
    advance(runtime, 30.0);
    audit(runtime);
    require(runtime.structure().paths.absorbedSolarJ == 0.0L, "Sky was not dark without an Orbit");
    require(runtime.structure().rates.absorbedSolarW == 0.0L, "A dark sky published a stellar rate");
    // A hand-authored profile is not a substitute for an authored Orbit: the
    // Orbit is the only thing that lights the native sky.
    auto authored = expandCellAtmosphere(scene);
    authored.thermal.solarCycle = station::ProfileSettings{1.0};
    CellAtmosphereTransport overridden(authored);
    require(overridden.advance(30.0, {}, 1u << 22, 1u << 22), "Profile-only playback refused");
    audit(overridden);
    require(overridden.structure().paths.absorbedSolarJ == 0.0L,
           "A solar profile lit the native sky without an authored Orbit");
    // The no-Hull opt-out stays absolute whatever orbit is authored.
    I optOut(2, 2);
    optOut.setHullType(std::nullopt);
    optOut.setOrbit(O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo});
    CellAtmosphereTransport none(optOut);
    require(none.structure().bodies.empty() && none.exteriorSolarFaces().empty(),
           "No-Hull opt-out gained exterior surface");
    require(none.advance(30.0, {}, 1u << 22, 1u << 22) && none.structure().paths.absorbedSolarJ == 0.0L,
           "No-Hull opt-out gained stellar input");
}

// The third state of the exposure rule: an authored Orbit plus a
// hand-authored solarCycle. The profile owns the whole stellar term and the
// orbital model is suppressed, which is observable because the profile
// contradicts what the orbital geometry would have produced at the same
// simulated instant.
void solarCycleOverridesTheOrbit() {
    // An orbit whose phase offset parks the whole run inside the eclipse.
    // The orbital model alone would deliver exactly nothing; a profile at
    // full level must still deliver full sun.
    I eclipsed(2, 2);
    eclipsed.setHullType(I::HullType::BareSkin);
    eclipsed.setOrbit(O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, 0.0, kIssPeriod / 2.0});
    const auto litByOrbitOnly = expandCellAtmosphere(eclipsed);
    CellAtmosphereTransport orbitOnly(litByOrbitOnly);
    advance(orbitOnly, 30.0);
    require(!orbitOnly.solarCycleOverrides(),
           "An Orbit with no authored profile must not report an override");
    require(orbitOnly.structure().paths.absorbedSolarJ == 0.0L,
           "The eclipsed Orbit alone delivered stellar energy before the override");

    auto overriddenExpansion = litByOrbitOnly;
    overriddenExpansion.thermal.solarCycle = station::ProfileSettings{1.0};
    CellAtmosphereTransport overridden(overriddenExpansion);
    require(overridden.solarCycleOverrides(), "Authored profile did not take the override");
    require(overridden.exposureEvaluator().dark(),
           "The exposure evaluator was built despite the profile override");
    advance(overridden, 30.0);
    audit(overridden);
    require(overridden.structure().paths.absorbedSolarJ > 0.0L,
           "The profile override delivered no stellar energy through an eclipse");

    // And the reverse: a profile at zero holds the hull dark through full
    // daylight, where the same orbit alone would have baked it.
    I noon(2, 2);
    noon.setHullType(I::HullType::BareSkin);
    noon.setOrbit(O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo});
    const auto noonGeometry = expandCellAtmosphere(noon);
    CellAtmosphereTransport sunlit(noonGeometry);
    advance(sunlit, 30.0);
    require(sunlit.structure().paths.absorbedSolarJ > 0.0L,
           "The unoverridden noon Orbit delivered no stellar energy");
    auto darkProfile = noonGeometry;
    darkProfile.thermal.solarCycle = station::ProfileSettings{0.0};
    CellAtmosphereTransport held(darkProfile);
    require(held.solarCycleOverrides(), "A zero profile did not take the override");
    advance(held, 30.0);
    audit(held);
    require(held.structure().paths.absorbedSolarJ == 0.0L,
           "A zero profile did not hold the hull dark through daylight");

    // The override is the legacy lumped application, so it is analytic:
    // sum(alpha * area / 4) * irradiance * level, with no per-face cosine
    // and no Planet shine. Assert that identity against the faces directly.
    I profiled(2, 2);
    profiled.setHullType(I::HullType::BareSkin);
    profiled.setOrbit(O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo});
    auto profiledGeometry = expandCellAtmosphere(profiled);
    profiledGeometry.thermal.solarCycle = station::ProfileSettings{0.5};
    CellAtmosphereTransport lumped(profiledGeometry);
    advance(lumped, 10.0);
    double lumpedWeight = 0.0;
    for (const auto& face : lumped.exteriorSolarFaces())
        lumpedWeight += face.absorptivity * face.areaM2 * 0.25;
    const double expected = lumpedWeight * kFullSun * 0.5 * 10.0;
    require(expected > 0.0 && std::abs(double(lumped.structure().paths.absorbedSolarJ) - expected) < 1e-6 * expected,
           "The profile override did not follow the lumped alpha * area / 4 rule");
    // The override leaves the sink bare: no Planet shine folded in, so a
    // nadir face radiates toward the authored Deep-space sink and not the
    // warm world the orbital model would have shown it.
    require(lumped.exposureEvaluator().planetViewFactor({0.0, 0.0, -1.0}) == 0.0,
           "Planet shine survived the profile override");

    // The override is new code in the hot thermal loop, so it holds the
    // same allocation-free contract as the orbital path.
    require(lumped.advance(0.1), "Override warm-up step refused");
    tracking = true;
    const bool overrideAdvanced = lumped.advance(0.1);
    tracking = false;
    require(overrideAdvanced && allocations == 0,
           "A profile-override thermal step allocated heap memory");

    // And the same determinism contract: a second transport over the same
    // authored profile, replayed to the same instant, lands on the same
    // stellar energy.
    CellAtmosphereTransport twin(profiledGeometry);
    advance(twin, 10.0);
    require(twin.advance(0.1) && twin.advance(0.1), "Override replay step refused");
    require(twin.structure().paths.absorbedSolarJ == lumped.structure().paths.absorbedSolarJ,
           "Two identical profile-override replays disagreed");
    audit(twin);
}

void successfulStepsAllocateNothingAndRefusalsRestoreEverything() {
    CellAtmosphereTransport runtime(litGeometry());
    require(runtime.advance(0.1), "Warm-up step refused");
    tracking = true;
    const bool advanced = runtime.advance(0.1);
    tracking = false;
    require(advanced && allocations == 0, "A successful orbital step allocated heap memory");
    tracking = true;
    for (int i = 0; i < 20; ++i) require(runtime.advance(0.05), "Repeated orbital step refused");
    tracking = false;
    require(allocations == 0, "Repeated orbital steps allocated heap memory");
    audit(runtime);

    const auto gas = std::vector<station::CellFluidState>(runtime.fluid().states().begin(), runtime.fluid().states().end());
    const auto bodies = runtime.structure().bodies;
    const auto ledger = runtime.sectors()[0].ledger;
    const auto paths = runtime.structure().paths;
    const auto elapsed = runtime.elapsedSeconds();
    require(!runtime.advance(1.0, {}, 4096, 0), "Refused step ignored the thermal work budget");
    for (std::size_t i = 0; i < gas.size(); ++i)
        require(runtime.fluid().states()[i].q == gas[i].q, "Refusal leaked gas state");
    require(bodies.size() == runtime.structure().bodies.size(), "Refusal changed body count");
    for (std::size_t i = 0; i < bodies.size(); ++i)
        require(bodies[i].energyJ() == runtime.structure().bodies[i].energyJ(), "Refusal leaked solid energy");
    require(runtime.sectors()[0].ledger == ledger, "Refusal leaked the atmosphere ledger");
    require(runtime.structure().paths == paths, "Refusal leaked the structural ledger");
    require(runtime.elapsedSeconds() == elapsed, "Refusal advanced the clock");
    audit(runtime);
    require(runtime.advance(0.1), "Retry after refusal failed");
    audit(runtime);
}

void identicalTimeGivesIdenticalResults() {
    auto run = [](std::vector<double> steps) {
        CellAtmosphereTransport runtime(litGeometry());
        for (const double step : steps) require(runtime.advance(step), "Deterministic run refused");
        return runtime;
    };
    const auto a = run({0.1, 0.1, 0.1});
    const auto b = run({0.1, 0.1, 0.1});
    require(a.elapsedSeconds() == b.elapsedSeconds(), "Determinism fixture drifted in time");
    require(a.structure().paths == b.structure().paths, "Identical time produced different structural ledgers");
    require(a.structure().bodies.size() == b.structure().bodies.size(), "Determinism fixture changed body count");
    for (std::size_t i = 0; i < a.structure().bodies.size(); ++i)
        require(a.structure().bodies[i].energyJ() == b.structure().bodies[i].energyJ(),
               "Identical time produced different hull temperatures");
    for (std::size_t i = 0; i < a.fluid().states().size(); ++i)
        require(a.fluid().states()[i].q == b.fluid().states()[i].q,
               "Identical time produced different gas state");
    // Eclipse entry lands on the same simulated instant every run.
    const auto c = run({0.1, 0.1, 0.1});
    require(c.structure().paths == a.structure().paths, "Replay diverged from the original run");
}

void anOpenDoorAdmitsNoDirectLight() {
    I scene(1, 3);
    scene.setHullType(I::HullType::BareSkin);
    scene.setOrbit(O{kFullSun, kIssPeriod, kIssRadius});
    const auto door = scene.level(0).verticalWall(0, 1);
    scene.setDoor(0, door, true);
    CellAtmosphereTransport runtime(expandCellAtmosphere(scene));
    auto litFaces = [&runtime] {
        return std::count_if(runtime.exteriorSolarFaces().begin(), runtime.exteriorSolarFaces().end(),
                            [](const auto& face) { return face.directScale == 1.0; });
    };
    const auto closed = litFaces();
    require(closed > 0, "Fixture has no exterior surface to switch off");
    require(runtime.setDoorOpen(0, door, true), "Door control refused");
    require(litFaces() < closed, "An open Door still passed direct stellar flux");
    require(runtime.setDoorOpen(0, door, false), "Door close refused");
    require(litFaces() == closed, "Closing the Door did not restore every exterior face");
    require(runtime.advance(0.1), "Door-cycled playback refused");
    audit(runtime);
}

void authoredDocumentIsImmutable() {
    I scene(2, 2);
    scene.setHullType(I::HullType::BareSkin);
    const O orbit{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo};
    scene.setOrbit(orbit);
    const auto authored = scene.serialize();
    CellAtmosphereTransport runtime(expandCellAtmosphere(scene));
    for (int i = 0; i < 10; ++i) advance(runtime, 0.1);
    require(scene.serialize() == authored, "Playback mutated the authored document");
    require(scene.orbit() == orbit, "Playback changed the authored orbit");
}

// The readouts the Orbit summary is built from - durations, Ground drift
// and the Geostationary condition - stay owned here rather than being
// re-derived by whatever draws them.
void summaryReadoutsTrackTheOrbit() {
    const StellarExposureEvaluator low{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo}};
    require(near(low.eclipseDurationSeconds() + low.sunlitDurationSeconds(), kIssPeriod, 1e-9),
            "Eclipse and sunlit durations do not sum to the Orbital period");
    require(near(low.eclipseDurationSeconds(), low.eclipseFraction() * kIssPeriod, 1e-12),
            "Eclipse duration is not the eclipse fraction over the Orbital period");
    // The spec's reference table: 35.9 min of eclipse for an ISS-like
    // orbit at Beta = 0.
    require(near(low.eclipseDurationSeconds() / 60.0, 35.9, 0.2),
            "ISS-like eclipse duration left the reference table");
    require(near(low.groundDriftDegreesPerDay(),
                 kSecondsPerDay * 360.0 * (1.0 / kIssPeriod - 1.0 / core::PlanetSiderealRotationS),
                 1e-6),
            "Ground drift is not the residue between the orbital and sidereal rates");
    require(low.groundDriftDegreesPerDay() > 0.0, "an under-synchronous orbit did not drift east");
    require(!low.geostationary(), "an ISS-like period read as Geostationary");
    require(near(low.orbitalPeriodSeconds(), kIssPeriod, 1e-12)
              && near(low.radiusFromPlanetCentreMeters(), kIssRadius, 1e-9),
            "the evaluator will not report the authored period and radius");

    const StellarExposureEvaluator held{O{kFullSun, core::PlanetSiderealRotationS, kGeoRadius,
                                        kEarthAlbedo}};
    require(held.geostationary(), "a sidereal Orbital period did not read as Geostationary");
    require(near(held.groundDriftDegreesPerDay(), 0.0, 1e-9),
            "a Geostationary orbit still reported Ground drift");
    // And the other row of the same table: 69.3 min of eclipse at
    // geostationary, Beta = 0.
    require(near(held.eclipseDurationSeconds() / 60.0, 69.3, 0.5),
            "geostationary eclipse duration left the reference table");

    // Either side of the match the condition goes, and the drift comes
    // back with it.
    for (double delta : {-0.1, 0.1, -1.0, 1.0}) {
        const StellarExposureEvaluator off{O{kFullSun, core::PlanetSiderealRotationS + delta,
                                           kGeoRadius, kEarthAlbedo}};
        require(!off.geostationary(),
                "the Geostationary condition survived a mismatched Orbital period");
        require(std::abs(off.groundDriftDegreesPerDay()) > 1e-5,
                "a period off the sidereal rotation still reported no drift");
    }
    // Past synchronous the residue changes sign: the ground outruns the
    // Installation and its sub-point creeps west.
    const StellarExposureEvaluator slow{O{kFullSun, core::PlanetSiderealRotationS * 1.5, kGeoRadius,
                                        kEarthAlbedo}};
    require(slow.groundDriftDegreesPerDay() < 0.0, "a super-synchronous orbit did not drift west");

    // Beta shortens the eclipse the same way it shortens the fraction, and
    // the pair still sums to the period. The phase offset moves the
    // eclipse along the clock and never resizes it.
    const StellarExposureEvaluator shallow{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, 0.6}};
    require(shallow.eclipseDurationSeconds() < low.eclipseDurationSeconds(),
            "growing Beta did not shorten the eclipse duration");
    require(near(shallow.eclipseDurationSeconds() + shallow.sunlitDurationSeconds(), kIssPeriod, 1e-9),
            "a tilted orbit stopped summing to the Orbital period");
    const StellarExposureEvaluator offset{O{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo, 0.6,
                                          900.0}};
    require(near(offset.eclipseDurationSeconds(), shallow.eclipseDurationSeconds(), 1e-12),
            "the phase offset resized the eclipse duration instead of moving it");
}

// With no sky there is nothing to derive, and the answer is unavailable
// rather than a zero that would read as a sunlit, driftless orbit.
void aDarkEvaluatorDerivesNothing() {
    const StellarExposureEvaluator dark;
    require(dark.dark(), "a default-constructed evaluator should be dark");
    require(std::isnan(dark.eclipseDurationSeconds()) && std::isnan(dark.sunlitDurationSeconds()),
            "a dark evaluator invented eclipse or sunlit durations");
    require(std::isnan(dark.groundDriftDegreesPerDay()), "a dark evaluator invented Ground drift");
    require(std::isnan(dark.orbitalPeriodSeconds()) && std::isnan(dark.radiusFromPlanetCentreMeters()),
            "a dark evaluator reported a period and radius it was never given");
    require(!dark.geostationary(), "a dark evaluator claimed Geostationary");
}
}

int main() {
    try {
        sunGeometry();
        eclipseFraction();
        eclipseFractionFollowsBeta();
        betaTiltsTheSunAcrossTrack();
        crossTrackFacesLightUpWithBeta();
        nadirNeverSeesDirectSun();
        phaseOffsetShiftsTheProfile();
        directFlux();
        planetInfraredIsDerived();
        planetViewFactorIsElementToSphere();
        planetShineMatchesTheReferenceTable();
        reflectedDiesInEclipseWhileInfraredContinues();
        effectiveSinkSeesThePlanet();
        refusedInputs();
        expansionCarriesTheOrbit();
        atMostHalfTheHullIsLit();
        betaAndPhaseReachTheTransport();
        stellarEnergyReachesTheLedger();
        theOrbitSetsTheIrradiance();
        planetShineSoftensTheEclipseTrough();
        planetaryInfraredIsNotSolarAndTheAuditCloses();
        noOrbitKeepsTheSkyDark();
        solarCycleOverridesTheOrbit();
        successfulStepsAllocateNothingAndRefusalsRestoreEverything();
        identicalTimeGivesIdenticalResults();
        anOpenDoorAdmitsNoDirectLight();
        authoredDocumentIsImmutable();
        summaryReadoutsTrackTheOrbit();
        aDarkEvaluatorDerivesNothing();
        std::cout << "Stellar exposure and lit-hull heating passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
