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
    const auto sampled = [](const StellarExposureEvaluator& evaluator, double period) {
        constexpr int samples = 20000;
        int eclipsed = 0;
        for (int i = 0; i < samples; ++i) {
            const double t = (i + 0.5) * period / samples;
            if (!evaluator.evaluate(t).sunlit) ++eclipsed;
        }
        return static_cast<double>(eclipsed) / samples;
    };
    require(std::abs(sampled(iss, kIssPeriod) - iss.eclipseFraction()) < 2.0 / 20000.0,
           "Instantaneous eclipse disagrees with the orbit-average fraction (ISS)");
    require(std::abs(sampled(geo, kGeoPeriod) - geo.eclipseFraction()) < 2.0 / 20000.0,
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
    // A Beta beyond the orbit pole is refused rather than swept.
    const StellarExposureEvaluator pole{O{kFullSun, kIssPeriod, kIssRadius}, 1.6};
    require(pole.dark(), "A Beta beyond the orbit pole was not refused");
}

CellAtmosphereExpansion litGeometry(double irradiance = kFullSun, double albedo = kEarthAlbedo) {
    I scene(2, 2);
    scene.setHullType(I::HullType::BareSkin);
    scene.setOrbit(O{irradiance, kIssPeriod, kIssRadius, albedo});
    return expandCellAtmosphere(scene);
}

void expansionCarriesTheOrbit() {
    I scene(1, 1);
    scene.setHullType(I::HullType::BareSkin);
    require(!expandThermal(scene).orbit, "Unauthored orbit must not appear in the expansion");
    const O orbit{kFullSun, kIssPeriod, kIssRadius, kEarthAlbedo};
    scene.setOrbit(orbit);
    const auto thermal = expandThermal(scene);
    require(thermal.orbit == orbit, "Bridge did not expose the authored Orbit");
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
    CellAtmosphereTransport runtime(scene);
    require(runtime.exposureEvaluator().dark(), "Transport built a sky with no authored Orbit");
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
}

int main() {
    try {
        sunGeometry();
        eclipseFraction();
        directFlux();
        planetInfraredIsDerived();
        planetViewFactorIsElementToSphere();
        planetShineMatchesTheReferenceTable();
        reflectedDiesInEclipseWhileInfraredContinues();
        effectiveSinkSeesThePlanet();
        refusedInputs();
        expansionCarriesTheOrbit();
        atMostHalfTheHullIsLit();
        stellarEnergyReachesTheLedger();
        theOrbitSetsTheIrradiance();
        planetShineSoftensTheEclipseTrough();
        planetaryInfraredIsNotSolarAndTheAuditCloses();
        noOrbitKeepsTheSkyDark();
        successfulStepsAllocateNothingAndRefusalsRestoreEverything();
        identicalTimeGivesIdenticalResults();
        anOpenDoorAdmitsNoDirectLight();
        authoredDocumentIsImmutable();
        std::cout << "Stellar exposure and lit-hull heating passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
