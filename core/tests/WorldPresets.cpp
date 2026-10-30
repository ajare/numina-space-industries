#include <nsi/core/Installation.h>
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

// An Earth-like ride: 420 km above the surface, the default Orbital period.
constexpr double kEarthRadiusM = 6371000.0;
constexpr double kAltitudeM = 420000.0;

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
    // The default world radius is the constant every document written
    // before this field existed was orbiting, so an absent field means the
    // same ground and nothing silently moves under an old save.
    const O defaults;
    require(defaults.planetRadiusM == kWorldPresets[2].planetRadiusM,
            "the default world radius is not Earth's");
    require(defaults.planetRadiusM == PlanetRadiusM, "Earth's row is not the planet constant");
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
    require(worldPresetMatching(authored) == 2, "an authored Earth did not read as Earth");
}

void everyWorldMakesAValidSky() {
    const O ride{1361.0, 5570.0, kEarthRadiusM + kAltitudeM, 0.3, 0.0, 0.0, kEarthRadiusM};
    for (std::size_t i = 0; i < kWorldPresets.size(); ++i) {
        const auto applied = applyWorldPreset(ride, i);
        try {
            applied.validate();
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("a world preset produced an invalid Orbit: ")
                                   + error.what());
        }
        require(applied.radiusFromPlanetCentreM > applied.planetRadiusM,
                "a preset left the orbit inside the world");
    }
}

void theRideIsLeftAlone() {
    const O ride{1361.0, 5570.0, kEarthRadiusM + kAltitudeM, 0.3, 12.0, 37.5, kEarthRadiusM};
    for (std::size_t i = 0; i < kWorldPresets.size(); ++i) {
        const auto applied = applyWorldPreset(ride, i);
        require(applied.orbitalPeriodS == ride.orbitalPeriodS,
                "a world preset changed the Orbital period");
        require(applied.phaseOffsetPercent == ride.phaseOffsetPercent,
                "a world preset moved the phase offset");
    }
}

void aBiggerWorldLiftsTheOrbitOffTheGround() {
    // 420 km above Earth. Jupiter is eleven times wider, so the orbit as
    // written is deep inside it; the preset moves the whole orbit out and
    // hands back the same 420 km of altitude over a different surface.
    const O ride{1361.0, 5570.0, kEarthRadiusM + kAltitudeM, 0.3, 0.0, 0.0, kEarthRadiusM};
    const auto jupiter = applyWorldPreset(ride, 4);
    require(jupiter.planetRadiusM == kWorldPresets[4].planetRadiusM, "Jupiter radius not applied");
    require(jupiter.radiusFromPlanetCentreM
                == kWorldPresets[4].planetRadiusM + kAltitudeM,
            "the orbit did not keep its altitude above the new surface");
    require(jupiter.radiusFromPlanetCentreM > ride.radiusFromPlanetCentreM,
            "the orbit did not move out for the wider world");
}

void aSmallerWorldLeavesTheOrbitWhereItWas() {
    // Shrinking the world cannot put the Hull inside it, so nothing moves.
    // An orbit only ever changes when it has to, never because a preset
    // thought it would look better elsewhere.
    const O ride{1361.0, 5570.0, kEarthRadiusM + kAltitudeM, 0.3, 0.0, 0.0, kEarthRadiusM};
    const auto mercury = applyWorldPreset(ride, 0);
    require(mercury.radiusFromPlanetCentreM == ride.radiusFromPlanetCentreM,
            "a smaller world moved an orbit that was never in danger");
    require(mercury.radiusFromPlanetCentreM > mercury.planetRadiusM,
            "the orbit ended up inside the smaller world");
}

void matchingIsExactAndHonest() {
    const O ride{1361.0, 5570.0, kEarthRadiusM + kAltitudeM, 0.3, 0.0, 0.0, kEarthRadiusM};
    for (std::size_t i = 0; i < kWorldPresets.size(); ++i) {
        const auto applied = applyWorldPreset(ride, i);
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
    }
    // A record that matches nothing reads as Custom rather than as the
    // nearest planet, which is the whole reason Custom is in the list.
    O none;
    none.albedo = 0.5;
    require(worldPresetMatching(none) == kNoWorldPreset, "an unmatched record claimed a world");
}

void anIndexPastTheCatalogueIsRefused() {
    const O ride{1361.0, 5570.0, kEarthRadiusM + kAltitudeM, 0.3, 0.0, 0.0, kEarthRadiusM};
    for (const std::size_t index : {kWorldPresets.size(), kWorldPresets.size() + 1,
                                    static_cast<std::size_t>(9999)}) {
        bool refused = false;
        try {
            (void)applyWorldPreset(ride, index);
        } catch (const std::out_of_range&) {
            refused = true;
        }
        require(refused, "an index past the catalogue was accepted");
    }
}

void deepSpaceIsTheAbsenceOfAWorldNotAZeroRow() {
    // "Deep space" - no star and no planet - is what an absent Orbit
    // record already means, so it needs no row in the catalogue. What it
    // must not be is a row of zeros: a world with no radius is not a
    // world, and a star with no light is not a star the model can reason
    // about. Both are refused, so nobody can spell "nothing" by typing 0
    // and get a sky that looks empty but is really invalid.
    O noStar;
    noStar.stellarIrradianceWPerM2 = 0.0;
    bool starRefused = false;
    try {
        noStar.validate();
    } catch (const std::invalid_argument&) {
        starRefused = true;
    }
    require(starRefused, "a zero-irradiance star was accepted as a way to spell dark");

    O noPlanet;
    noPlanet.planetRadiusM = 0.0;
    bool planetRefused = false;
    try {
        noPlanet.validate();
    } catch (const std::invalid_argument&) {
        planetRefused = true;
    }
    require(planetRefused, "a zero-radius world was accepted as a way to spell empty");

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
        theRideIsLeftAlone();
        aBiggerWorldLiftsTheOrbitOffTheGround();
        aSmallerWorldLeavesTheOrbitWhereItWas();
        matchingIsExactAndHonest();
        anIndexPastTheCatalogueIsRefused();
        deepSpaceIsTheAbsenceOfAWorldNotAZeroRow();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "world presets ok\n";
    return 0;
}
