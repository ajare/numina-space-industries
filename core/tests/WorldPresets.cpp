#include <nsi/core/Installation.h>
#include <nsi/core/Units.h>
#include <nsi/core/WorldPresets.h>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace nsi::core;
using O = Installation::Orbit;

namespace {

void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
bool near(double value, double expected, double tolerance) {
    return std::abs(value - expected) <= tolerance;
}

// The published obliquity to orbit for each world, Sun outward, from the
// NASA NSSDCA Planetary Fact Sheet. The table under test stores the
// folded value; keeping the published one here is what makes the fold
// checkable rather than just asserted.
constexpr std::array<double, 8> kPublishedObliquityDeg{
    0.034, 177.4, 23.4, 25.2, 3.1, 26.7, 97.8, 28.3};

// An Earth-like ride at a chosen tilt and phase: the authored height above
// an Earth-sized, Earth-mass world. Built through the defaults rather than
// a positional literal so that a change to the field order shows up as a
// changed test rather than as a silently different world.
constexpr O earthRide(double tiltDeg = 0.0, double phasePercent = 0.0) {
    O ride;
    ride.axialTiltDeg = tiltDeg;
    ride.phaseOffsetPercent = phasePercent;
    return ride;
}

// The Kepler period recomputed here, independently of the record, so the
// derivation under test is checked against the law rather than against
// itself.
double keplerPeriodSeconds(double massKg, double radiusFromCentreM) {
    return 2.0 * kPi
         * std::sqrt(radiusFromCentreM * radiusFromCentreM * radiusFromCentreM
                     / (GravitationalConstantM3PerKgS2 * massKg));
}

void tableIsWellFormed() {
    require(kWorldPresets.size() == 8, "the catalogue is not the eight planets");
    constexpr std::array<std::string_view, 8> expected{
        "Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune"};
    for (std::size_t i = 0; i < kWorldPresets.size(); ++i) {
        const auto& world = kWorldPresets[i];
        require(world.name == expected[i], "world catalogue out of order or misnamed");
        require(std::isfinite(world.stellarIrradianceWPerM2)
                    && world.stellarIrradianceWPerM2 > 0.0,
                "a world has a non-finite or non-positive irradiance");
        require(world.albedo >= 0.0 && world.albedo <= 1.0, "a world albedo left [0, 1]");
        require(std::isfinite(world.axialTiltDeg) && world.axialTiltDeg > -90.0
                    && world.axialTiltDeg < 90.0,
                "a world tilt left the model's (-90, 90) range");
        require(std::isfinite(world.planetRadiusM) && world.planetRadiusM > 0.0,
                "a world has a non-finite or non-positive radius");
        // A row without a mass can draw the ground and still not know how
        // long the ride round it takes, which is the whole reason the
        // column exists.
        require(std::isfinite(world.planetMassKg) && world.planetMassKg > 0.0,
                "a world has a non-finite or non-positive mass");
        require(std::isfinite(world.siderealRotationS) && world.siderealRotationS > 0.0,
                "a world has a non-finite or non-positive sidereal rotation");
        // The fold is what puts the retrograde worlds back inside the
        // range: nothing in the table may disagree with folding its own
        // published obliquity.
        require(world.axialTiltDeg == foldAxialTiltDegrees(kPublishedObliquityDeg[i]),
                "a world tilt is not its published obliquity folded");
    }
}

void theFoldCoversTheRetrogradeWorlds() {
    require(foldAxialTiltDegrees(0.0) == 0.0, "zero obliquity did not fold to zero");
    require(foldAxialTiltDegrees(23.4) == 23.4, "a prograde obliquity was bent");
    require(foldAxialTiltDegrees(90.0) == 90.0, "the pole itself moved");
    // Venus spins backwards almost upright, and Uranus lies on its side.
    // Neither can be a tilt in this model as published; both fold to the
    // latitude their star actually sits over at solstice. Tolerances, not
    // equality: 180 - 177.4 lands on 2.5999999999999943, which is the
    // right answer and not the same double as the literal 2.6.
    require(near(foldAxialTiltDegrees(177.4), 2.6, 1e-9), "Venus did not fold to 2.6 deg");
    require(near(foldAxialTiltDegrees(97.8), 82.2, 1e-9), "Uranus did not fold to 82.2 deg");
    for (double obliquity = 0.0; obliquity <= 180.0; obliquity += 0.5) {
        const double folded = foldAxialTiltDegrees(obliquity);
        require(folded >= 0.0 && folded <= 90.0, "the fold left [0, 90]");
    }
}

void theDefaultRecordIsEarthSizedButNotAWorld() {
    // The default world is the one every document written before these
    // fields existed was orbiting, so an absent field means the same
    // ground and nothing silently moves under an old save.
    const O defaults;
    require(defaults.planetRadiusM == kWorldPresets[2].planetRadiusM,
            "the default world radius is not Earth's");
    require(defaults.planetRadiusM == PlanetRadiusM, "Earth's row is not the planet constant");
    require(defaults.planetMassKg == kWorldPresets[2].planetMassKg,
            "the default world mass is not Earth's");
    require(defaults.siderealRotationS == kWorldPresets[2].siderealRotationS,
            "the default day length is not Earth's");
    // But the default sky is deliberately not Earth. Its tilt is zero -
    // the deepest eclipse the model can hand out, so a new document starts
    // pessimistic and the author relaxes it on purpose - while Earth's is
    // 23.4. Reading that as "Earth" would tell the author something false
    // about how much eclipse they were starting with, so it reads Custom.
    require(defaults.axialTiltDeg == 0.0, "the default tilt stopped being the worst case");
    require(worldPresetMatching(defaults) == kNoWorldPreset,
            "the worst-case default sky claimed to be a real world");
    // And the Earth row itself does read as Earth, once authored.
    O authored;
    authored.stellarIrradianceWPerM2 = 1361.0;
    authored.albedo = 0.306;
    authored.axialTiltDeg = 23.4;
    authored.planetRadiusM = 6371000.0;
    authored.planetMassKg = 5.9724e24;
    authored.siderealRotationS = 86164.0;
    require(worldPresetMatching(authored) == 2, "an authored Earth did not read as Earth");
}

void everyWorldMakesAValidSky() {
    for (std::size_t i = 0; i < kWorldPresets.size(); ++i) {
        const auto applied = applyWorldPreset(earthRide(), i);
        try {
            applied.validate();
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("a world preset produced an invalid Orbit: ")
                                   + error.what());
        }
        require(applied.orbitalRadiusM() > applied.planetRadiusM,
                "a preset left the orbit inside the world");
        require(std::isfinite(applied.orbitalPeriodSeconds())
                    && applied.orbitalPeriodSeconds() > 0.0,
                "a preset world cannot derive an Orbital period");
    }
}

// The height and the phase are the ride, and a world change leaves both
// exactly where the author put them. The Orbital period is not left alone
// because it is no longer a thing to leave: it is derived, so it moves
// because the world moved under it.
void theRideIsLeftAloneAndThePeriodFollowsTheWorld() {
    const O ride = earthRide(12.0, 37.5);
    double previousPeriod = 0.0;
    for (std::size_t i = 0; i < kWorldPresets.size(); ++i) {
        const auto applied = applyWorldPreset(ride, i);
        require(applied.heightAboveGroundM == ride.heightAboveGroundM,
                "a world preset changed the height above ground");
        require(applied.phaseOffsetPercent == ride.phaseOffsetPercent,
                "a world preset moved the phase offset");
        const double period = applied.orbitalPeriodSeconds();
        require(std::isfinite(period) && period > 0.0,
                "a preset world has no derivable Orbital period");
        // Every world has a different mass, so the same height above each
        // of them cannot take the same time. If two agreed to the last bit
        // the period would not be reading the mass at all.
        if (i > 0)
            require(period != previousPeriod,
                    "the Orbital period did not change between worlds of different mass");
        previousPeriod = period;
    }
    // The same 420 km, two worlds, nothing alike. These are the numbers
    // the author used to have to look up and type by hand.
    require(near(applyWorldPreset(earthRide(), 2).orbitalPeriodSeconds(), 5569.33, 0.01),
            "420 km about Earth is not about 5,569 s");
    require(near(applyWorldPreset(earthRide(), 4).orbitalPeriodSeconds(), 10411.81, 0.01),
            "420 km about Jupiter is not about 10,412 s");
}

// Because the height is measured from the surface, a wider world cannot
// swallow an orbit the way an authored radius-from-centre could: the
// ground comes up to meet the Installation and the height it was told
// about is still the height it has. The two carve-outs this replaces -
// lifting an orbit off a wider world, leaving it put on a narrower one -
// collapse into this one fact.
void noWorldCanSwallowTheOrbit() {
    const auto mercury = applyWorldPreset(earthRide(), 0);
    const auto jupiter = applyWorldPreset(earthRide(), 4);
    require(mercury.heightAboveGroundM == earthRide().heightAboveGroundM
                && jupiter.heightAboveGroundM == earthRide().heightAboveGroundM,
            "a world change moved the height above the surface");
    require(jupiter.orbitalRadiusM()
                == kWorldPresets[4].planetRadiusM + earthRide().heightAboveGroundM,
            "the orbital radius is not the new world plus the authored height");
    require(mercury.orbitalRadiusM()
                == kWorldPresets[0].planetRadiusM + earthRide().heightAboveGroundM,
            "the orbital radius is not the new world plus the authored height");
    require(jupiter.planetRadiusM > mercury.planetRadiusM,
            "the test worlds are not ordered as intended");
}

void thePeriodIsKepler() {
    // Checked against the law recomputed here rather than against the
    // record's own answer, so a derivation that drifted would not simply
    // agree with itself.
    const O earth = applyWorldPreset(earthRide(), 2);
    require(near(earth.orbitalPeriodSeconds(),
                keplerPeriodSeconds(earth.planetMassKg, earth.orbitalRadiusM()), 1e-6),
            "the Orbital period is not Kepler's third law over the authored mass and radius");
    const O jupiter = applyWorldPreset(earthRide(), 4);
    require(near(jupiter.orbitalPeriodSeconds(),
                keplerPeriodSeconds(jupiter.planetMassKg, jupiter.orbitalRadiusM()), 1e-6),
            "the Orbital period is not Kepler's third law over the authored mass and radius");
    // Climbing takes longer, and by the amount the law says rather than the
    // amount a hand-tuned field would have said.
    O higher = earth;
    higher.heightAboveGroundM *= 2.0;
    require(higher.orbitalPeriodSeconds() > earth.orbitalPeriodSeconds(),
            "climbing higher did not lengthen the Orbital period");
    require(near(higher.orbitalPeriodSeconds() / earth.orbitalPeriodSeconds(),
                std::sqrt(std::pow(higher.orbitalRadiusM() / earth.orbitalRadiusM(), 3.0)),
                1e-9),
            "the period did not scale as the three-halves power of the radius");
}

// The synchronous height is the height at which the derived period comes
// back equal to the world's own day. Round-tripping it is the check that
// the readout shown beside the height field and the ride it describes are
// the same number, not two derivations that happen to look alike.
void theSynchronousHeightRoundTripsToTheDay() {
    for (std::size_t i = 0; i < kWorldPresets.size(); ++i) {
        const auto world = applyWorldPreset(earthRide(), i);
        const double sync = world.synchronousHeightM();
        require(std::isfinite(sync) && sync > 0.0,
                "a world has no derivable synchronous height");
        O riding = world;
        riding.heightAboveGroundM = sync;
        require(near(riding.orbitalPeriodSeconds(), world.siderealRotationS,
                    world.siderealRotationS * 1e-9),
                "riding at the synchronous height does not give the world's own day");
    }
    // Earth's is the familiar one, and worth pinning: it is the number an
    // author is actually shown next to the height field.
    const O earth = applyWorldPreset(earthRide(), 2);
    require(near(earth.synchronousHeightM(), 35793685.0, 1.0),
            "Earth's synchronous height is not about 35,794 km");
}

void matchingIsExactAndHonest() {
    for (std::size_t i = 0; i < kWorldPresets.size(); ++i) {
        const auto applied = applyWorldPreset(earthRide(), i);
        require(worldPresetMatching(applied) == i, "a freshly applied world did not match itself");
        // The smallest nudge to any world field is no longer that world.
        auto nudged = applied;
        nudged.albedo += 0.001;
        require(worldPresetMatching(nudged) == kNoWorldPreset,
                "a hand-tuned albedo still read as the preset world");
        nudged = applied;
        nudged.planetRadiusM += 1.0;
        require(worldPresetMatching(nudged) == kNoWorldPreset,
                "a hand-tuned radius still read as the preset world");
        // Mass is a world field like any other: a hand-tuned one is a
        // different world, and saying so is what keeps the derived period
        // honest about where it came from.
        nudged = applied;
        nudged.planetMassKg *= 1.001;
        require(worldPresetMatching(nudged) == kNoWorldPreset,
                "a hand-tuned mass still read as the preset world");
        nudged = applied;
        nudged.siderealRotationS += 1.0;
        require(worldPresetMatching(nudged) == kNoWorldPreset,
                "a hand-tuned day length still read as the preset world");
    }
    // A record that matches nothing reads as Custom rather than as the
    // nearest planet, which is the whole reason Custom is in the list.
    O none;
    none.albedo = 0.5;
    require(worldPresetMatching(none) == kNoWorldPreset, "an unmatched record claimed a world");
}

void anIndexPastTheCatalogueIsRefused() {
    for (const std::size_t index : {kWorldPresets.size(), kWorldPresets.size() + 1,
                                    static_cast<std::size_t>(9999)}) {
        bool refused = false;
        try {
            (void)applyWorldPreset(earthRide(), index);
        } catch (const std::out_of_range&) {
            refused = true;
        }
        require(refused, "an index past the catalogue was accepted");
    }
}

void refusedAsAWayToSpellNothing() {
    // A broken world is refused on the way in rather than left to answer
    // NaN downstream, where a missing mass would read as a broken readout
    // instead of the broken world it actually is.
    struct Case {
        const char* what;
        void (*breakIt)(O&);
    };
    const std::array<Case, 6> cases{{
        {"a zero-irradiance star", [](O& o) { o.stellarIrradianceWPerM2 = 0.0; }},
        {"a zero-radius world", [](O& o) { o.planetRadiusM = 0.0; }},
        {"a zero-mass world", [](O& o) { o.planetMassKg = 0.0; }},
        {"a negative-mass world", [](O& o) { o.planetMassKg = -1.0; }},
        {"a zero-length day", [](O& o) { o.siderealRotationS = 0.0; }},
        {"a height on the ground", [](O& o) { o.heightAboveGroundM = 0.0; }},
    }};
    for (const auto& testCase : cases) {
        O broken;
        testCase.breakIt(broken);
        bool refused = false;
        try {
            broken.validate();
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        require(refused, testCase.what);
    }
    // And a derivation over a broken world says it cannot answer rather
    // than handing back a plausible zero.
    O massless;
    massless.planetMassKg = 0.0;
    require(std::isnan(massless.orbitalPeriodSeconds()),
            "a massless world still produced a period");
    require(std::isnan(massless.synchronousHeightM()),
            "a massless world still produced a synchronous height");

    // And the catalogue stays a list of worlds. Deep space is not in it,
    // because it is not a world - it is the record not being there.
    for (const auto& world : kWorldPresets)
        require(world.name != "Deep space", "Deep space was added as a world row");

    // The absence itself is always available and always means the same
    // thing: no world authored at all, which the bridge reads as a dark
    // sky with no stellar gain.
    const std::optional<O> nothing;
    require(!nothing.has_value(), "an absent Orbit claimed a world");
}

} // namespace

int main() {
    try {
        tableIsWellFormed();
        theFoldCoversTheRetrogradeWorlds();
        theDefaultRecordIsEarthSizedButNotAWorld();
        everyWorldMakesAValidSky();
        theRideIsLeftAloneAndThePeriodFollowsTheWorld();
        noWorldCanSwallowTheOrbit();
        thePeriodIsKepler();
        theSynchronousHeightRoundTripsToTheDay();
        matchingIsExactAndHonest();
        anIndexPastTheCatalogueIsRefused();
        refusedAsAWayToSpellNothing();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "world presets ok\n";
    return 0;
}
