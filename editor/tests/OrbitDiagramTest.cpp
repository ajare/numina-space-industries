#include "OrbitDiagram.h"
#include "OrbitSummary.h"

#include <nsi/systems/StellarExposure.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace station::editor;
using I = nsi::core::Installation;
using O = I::Orbit;
using nsi::systems::StellarExposureEvaluator;

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = 3.14159265358979323846;
// The documented default ride: Earth, 420 km up, noon to open. The
// Orbital period is derived from the world's mass and this height rather
// than authored, so the tests read it back off the record instead of
// carrying a constant that could fall out of step with the derivation.
constexpr double kEarthRadiusM = 6371000.0;
constexpr double kDefaultOrbitM = 6791000.0;

void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
bool near(double value, double expected, double tolerance) {
    return std::abs(value - expected) <= tolerance;
}
// Angles compared by direction rather than by number, so the wrap at +/-pi
// and the difference between [0, 2pi) and (-pi, pi] cannot fail a test
// that is only asking whether the two point the same way.
bool sameDirection(double a, double b, double tolerance) {
    return near(std::cos(a), std::cos(b), tolerance) && near(std::sin(a), std::sin(b), tolerance);
}

// A canvas big enough not to be the thing under test.
OrbitDiagramLayout place(double planetRadiusM, double orbitRadiusM, double angleRad) {
    return orbitDiagramLayout(planetRadiusM, orbitRadiusM, angleRad, 128.0, 20.0, 4.5);
}

O defaultOrbit() {
    O orbit;
    orbit.heightAboveGroundM = kDefaultOrbitM - kEarthRadiusM;
    orbit.planetRadiusM = kEarthRadiusM;
    return orbit;
}

// Where the dot sits when a run opens under a given phase offset.
OrbitDiagramLayout openingAt(double phasePercent) {
    O orbit = defaultOrbit();
    orbit.phaseOffsetPercent = phasePercent;
    const StellarExposureEvaluator evaluator{orbit};
    return place(kEarthRadiusM, kDefaultOrbitM, orbitAngleRadians(evaluator.evaluate(0.0)));
}

// The clock face. Local noon is the top because that is where the star is,
// and local midnight is the bottom because that is where the shadow is.
void noonIsAtTwelveOClock() {
    const auto layout = place(kEarthRadiusM, kDefaultOrbitM, 0.0);
    require(layout.valid, "the default world and ride could not be placed");
    require(near(layout.installationX, 0.0, 1e-9), "noon is not on the vertical");
    require(layout.installationY < 0.0, "noon is below the world, not above it");
    require(near(layout.installationY, -layout.orbitRadiusPx, 1e-9),
           "noon is not on the ring at the top");
}

void localMidnightIsAtSixOClock() {
    const auto layout = place(kEarthRadiusM, kDefaultOrbitM, kPi);
    require(layout.valid, "half a turn could not be placed");
    require(near(layout.installationX, 0.0, 1e-9), "midnight is not on the vertical");
    require(layout.installationY > 0.0, "midnight is above the world, not below it");
    // The bottom of the ring is the middle of the shadow, which is the
    // whole reason the top is noon.
    require(near(layout.installationY, layout.orbitRadiusPx, 1e-9),
           "midnight is not on the ring at the bottom");
}

void quarterTurnsGoClockwiseRoundTheFace() {
    const auto quarter = place(kEarthRadiusM, kDefaultOrbitM, kPi / 2.0);
    require(quarter.valid, "a quarter turn could not be placed");
    require(quarter.installationX > 0.0, "a quarter turn went left, not right");
    require(near(quarter.installationY, 0.0, 1e-9), "a quarter turn left the horizontal");
    const auto threeQuarter = place(kEarthRadiusM, kDefaultOrbitM, 3.0 * kPi / 2.0);
    require(threeQuarter.valid, "three quarters could not be placed");
    require(threeQuarter.installationX < 0.0, "three quarters went right, not left");
}

// One scale for both radii is what makes "the correct distance, scaled"
// mean anything: the ring is the authored distance against the authored
// world, so nothing is stretched relative to anything else.
void oneScaleServesBothRadii() {
    const auto layout = place(kEarthRadiusM, kDefaultOrbitM, 0.0);
    require(layout.valid, "the default pair could not be placed");
    const double metreRatio = kEarthRadiusM / kDefaultOrbitM;
    const double pixelRatio = layout.planetRadiusPx / layout.orbitRadiusPx;
    require(near(pixelRatio, metreRatio, 1e-12),
           "the world and the ring are not on one scale");
    require(layout.orbitRadiusPx <= 128.0 - 20.0, "the ring runs past the canvas");
    require(layout.orbitRadiusPx > 0.0, "the ring has no size");
}

void aLowOrbitRidesTheLimb() {
    // 420 km over a 6371 km world is a 6% band, and the diagram is meant
    // to show that rather than hide it. A world that leaves the dot a
    // comfortable gap would be a lie about low orbit.
    const auto layout = place(kEarthRadiusM, kDefaultOrbitM, 0.0);
    const double band = layout.orbitRadiusPx - layout.planetRadiusPx;
    require(band > 0.0, "the ring is inside the world");
    require(band / layout.orbitRadiusPx < 0.07, "a low orbit has too much air in it");
}

void aFarOrbitLeavesTheWorldSmall() {
    // Geostationary height, same world: the world has to shrink with the
    // ring's growth and not stay a disc the ring is barely outside of.
    const auto layout = place(kEarthRadiusM, 42164000.0, 0.0);
    require(layout.valid, "a high orbit could not be placed");
    const double ratio = layout.planetRadiusPx / layout.orbitRadiusPx;
    require(near(ratio, kEarthRadiusM / 42164000.0, 1e-12), "the scale drifted with the height");
    require(ratio < 0.16, "the world did not shrink out of the way of the ring");
}

void theDotStaysInsideTheFrame() {
    for (int step = 0; step < 24; ++step) {
        const double angle = step * 2.0 * kPi / 24.0;
        const auto layout = place(kEarthRadiusM, kDefaultOrbitM, angle);
        require(layout.valid, "a step round the face could not be placed");
        const double reach = std::hypot(layout.installationX, layout.installationY) + 4.5;
        require(reach <= 128.0, "the dot leaves the canvas part way round");
    }
}

// The angle is read out of the evaluator's own star vector rather than
// recomputed from the clock, so this pins the read-back against the
// sweep the evaluator documents: turns of t / period, noon at zero.
void theAngleIsTheEvaluatorsOwnSweep() {
    O orbit = defaultOrbit();
    const StellarExposureEvaluator evaluator{orbit};
    const double periodS = orbit.orbitalPeriodSeconds();
    for (int step = 0; step <= 20; ++step) {
        const double turns = step / 20.0;
        const double elapsed = turns * periodS;
        const double angle = orbitAngleRadians(evaluator.evaluate(elapsed));
        require(sameDirection(angle, turns * 2.0 * kPi, 1e-9),
              "the read-back angle is not the evaluator's own phase");
    }
}

// Leaning the world moves the shadow across the ride; it does not move
// the ride. If the tilt leaked into the angle the dot would drift round
// the face every time the author touched the tilt, and there would be no
// reason on earth for it.
void theTiltMovesTheShadowNotTheRide() {
    for (double tilt : {0.0, 12.5, 45.0, 89.0}) {
        O orbit = defaultOrbit();
        orbit.axialTiltDeg = tilt;
        const double periodS = orbit.orbitalPeriodSeconds();
        const StellarExposureEvaluator evaluator{orbit};
        for (int step = 0; step <= 8; ++step) {
            const double elapsed = step * periodS / 8.0;
            const double untilted =
                orbitAngleRadians(StellarExposureEvaluator{defaultOrbit()}.evaluate(elapsed));
            require(sameDirection(orbitAngleRadians(evaluator.evaluate(elapsed)), untilted, 1e-9),
                    "the axial tilt moved the ride around the world");
        }
    }
}

// The phase offset is where the run opens. Half way round the cycle means
// the dot starts at the bottom, in the shadow, with nothing else about
// the picture changed.
void thePhaseOffsetMovesWhereTheDotOpens() {
    O orbit = defaultOrbit();
    orbit.phaseOffsetPercent = 50.0;
    const StellarExposureEvaluator evaluator{orbit};
    require(sameDirection(orbitAngleRadians(evaluator.evaluate(0.0)), kPi, 1e-9),
           "a half-cycle offset did not open at the bottom");
    orbit.phaseOffsetPercent = 0.0;
    const StellarExposureEvaluator plain{orbit};
    require(sameDirection(orbitAngleRadians(plain.evaluate(0.0)), 0.0, 1e-9),
           "a zero offset did not open at the top");
}

// The band is the model's shadow, not a second opinion about it: half
// the angle it covers is the evaluator's own orbit-average blocked
// fraction turned into degrees, and that fraction is the geometric
// shadow of a cylinder of the world's radius across a ring of the
// orbit's.
void theBandIsTheWorldsOwnShadow() {
    const O orbit = defaultOrbit();
    const StellarExposureEvaluator evaluator{orbit};
    const double halfWidthDeg = evaluator.eclipseFraction() * 180.0;
    const double geometricDeg =
        std::asin(kEarthRadiusM / kDefaultOrbitM) * 180.0 / kPi;
    require(near(halfWidthDeg, geometricDeg, 0.05),
           "the band is not the shadow the world actually casts");
    require(halfWidthDeg > 0.0 && halfWidthDeg < 90.0,
           "the band is not a wedge - it must stay convex to be filled");
}

// A tilt great enough carries the ring clear of the shadow, and then
// there is no band to draw. The diagram has to be able to say that, and
// not fall back on the ramp, which would otherwise hand back half a
// solar degree of band around a shadow that was never there.
void aClearOrbitHasNoBand() {
    O orbit = defaultOrbit();
    // Well past the critical tilt for this height: nothing is eclipsed.
    orbit.axialTiltDeg = 89.0;
    const StellarExposureEvaluator evaluator{orbit};
    require(evaluator.eclipseFraction() == 0.0, "a cleared orbit still blocks something");
    require(evaluator.umbraHalfWidthDegrees() == 0.0, "a cleared orbit still has an umbra");
    // The gate the drawing uses: no shadow, so no band of either kind.
    require(!(evaluator.eclipseFraction() > 0.0), "the band gate opened on no shadow");
}

// The ramp straddles the geometric edge rather than sitting inside it or
// outside it: the umbra stops half a solar degree short, the penumbra
// reaches half a solar degree past. That is what makes the geometric
// shadow the 50% line rather than the totality line, and why the umbra
// is not simply the old single band.
void theRampStraddlesTheGeometricEdge() {
    const StellarExposureEvaluator evaluator{defaultOrbit()};
    const double geometric = evaluator.eclipseFraction() * 180.0;
    const double ramp = evaluator.penumbraWidthDegrees();
    require(near(ramp, 0.533, 1e-12), "the ramp is not one solar diameter");
    require(near(evaluator.umbraHalfWidthDegrees(), geometric - ramp / 2.0, 1e-9),
            "the umbra does not stop half a solar degree short of the geometric edge");
    require(near(evaluator.penumbraOuterHalfWidthDegrees(), geometric + ramp / 2.0, 1e-9),
            "the penumbra does not reach half a solar degree past the geometric edge");
    require(near(evaluator.penumbraOuterHalfWidthDegrees() - evaluator.umbraHalfWidthDegrees(),
                 ramp, 1e-9), "the band is not one ramp wide on each side");
    require(evaluator.umbraHalfWidthDegrees() < geometric,
            "the umbra is no narrower than the geometric shadow");
}

// Walk the tilt up to where the eclipse dies. Past the point where the
// two ramps overlap there is no totality, but there is still ramp, and
// the umbra has to floor at zero rather than report a negative shadow
// that would draw as a wedge on the wrong side of the face.
void aGrazingPassFloorsTheUmbraAtZero() {
    bool sawTotality = false;
    bool sawRampWithoutTotality = false;
    for (double tilt = 0.0; tilt < 90.0; tilt += 0.05) {
        O orbit = defaultOrbit();
        orbit.axialTiltDeg = tilt;
        const StellarExposureEvaluator evaluator{orbit};
        const double umbra = evaluator.umbraHalfWidthDegrees();
        const double outer = evaluator.penumbraOuterHalfWidthDegrees();
        require(umbra >= 0.0, "the umbra went negative on a grazing pass");
        require(outer >= umbra, "the penumbra outer edge fell inside the umbra");
        if (umbra > 0.0) sawTotality = true;
        if (umbra == 0.0 && outer > 0.0) sawRampWithoutTotality = true;
    }
    require(sawTotality, "no tilt at all produced totality - the sweep is wrong");
    require(sawRampWithoutTotality,
            "no tilt lost its totality while keeping its ramp - the floor is untested");
}

// Half a solar degree at a canvas anyone looks at is a fraction of a
// pixel, so the ramp gets a floor in pixels. The floor is a floor and
// not a target: a ramp wide enough already is left exactly as it is.
void theRampIsWidenedUntilItCanBeSeen() {
    constexpr double kRamp = 0.533;
    const double widened = drawnPenumbraWidthDegrees(kRamp, 100.0, 7.0);
    const double floorDeg = 7.0 / 100.0 * 180.0 / kPi;
    require(near(widened, floorDeg, 1e-9), "the ramp was not widened to the pixel floor");
    require(widened > kRamp, "the widened ramp is not wider than the true one");
    require(drawnPenumbraWidthDegrees(20.0, 100.0, 7.0) == 20.0,
            "a ramp that already showed was stretched past what it needed");
    // A big enough canvas needs no help at all.
    require(drawnPenumbraWidthDegrees(kRamp, 100000.0, 7.0) == kRamp,
            "a big canvas was made to exaggerate a ramp that already fits");
    // Nonsense comes back as nothing to draw, never as a negative band.
    require(drawnPenumbraWidthDegrees(kNaN, 100.0, 7.0) == 0.0, "a NaN ramp was drawn");
    require(drawnPenumbraWidthDegrees(-5.0, 100.0, 7.0) == 0.0, "a negative ramp was drawn");
    require(drawnPenumbraWidthDegrees(0.0, 100.0, 7.0) == 0.0, "a zero ramp was drawn");
    // A ring with no radius cannot turn pixels into degrees; hand back
    // the true width rather than dividing by it.
    require(drawnPenumbraWidthDegrees(kRamp, 0.0, 7.0) == kRamp,
            "a zero ring lost the ramp to a division");
    require(drawnPenumbraWidthDegrees(kRamp, 100.0, 0.0) == kRamp,
            "a zero floor stretched the ramp anyway");
}

// What the author reads as 180 deg on the phase slider has to be half
// the cycle in the record and the bottom of the face, in the shadow.
// The trap this pins: ImGui's SliderAngle takes its value in RADIANS and
// its bounds in DEGREES, so handing it degrees stores 0.87% instead of
// 50% - the dot parked 3.1 deg past noon, sunlit, near the top - while
// the widget converted on the way back out and kept reading a truthful
// 180.0 deg over a phase that was nowhere near it.
void whatTheSliderCallsEighteenOpensTheRunInTheShadow() {
    const double shown = 180.0;
    require(near(offsetPercentFromPhaseAngle(shown), 50.0, 1e-12),
            "180 deg on the face is not half the cycle");
    const auto opening = openingAt(offsetPercentFromPhaseAngle(shown));
    require(opening.valid, "the half-cycle opening could not be placed");
    require(opening.installationY > 0.0, "the run does not open below the world");
    require(near(opening.installationX, 0.0, 1e-9), "the run does not open on the vertical");

    // The mix-up itself, named and pinned, so the guard says what it is
    // guarding rather than just being a test that passes.
    const double radianMixUp = shown * (kPi / 180.0) * 100.0 / 360.0;
    require(radianMixUp < 1.0, "the guarded mix-up is not the small value expected");
    const auto mistaken = openingAt(radianMixUp);
    require(mistaken.valid && mistaken.installationY < 0.0,
            "the guarded mix-up does not land near the top as described");
    require(mistaken.installationX > 0.0,
            "the guarded mix-up does not sit just past noon as described");
}

void nonsenseIsRefusedRatherThanDrawn() {
    require(!place(kEarthRadiusM, kDefaultOrbitM, kNaN).valid, "a NaN angle was placed");
    require(!place(0.0, kDefaultOrbitM, 0.0).valid, "a world with no radius was placed");
    require(!place(-1.0, kDefaultOrbitM, 0.0).valid, "a world inside out was placed");
    require(!place(kEarthRadiusM, 0.0, 0.0).valid, "a ring with no radius was placed");
    require(!place(kNaN, kDefaultOrbitM, 0.0).valid, "a world of no size was placed");
    require(!place(kEarthRadiusM, kDefaultOrbitM, std::numeric_limits<double>::infinity()).valid,
           "an angle that never ends was placed");
    // A canvas with no room in it says so instead of dividing by it.
    require(!orbitDiagramLayout(kEarthRadiusM, kDefaultOrbitM, 0.0, 10.0, 20.0, 4.5).valid,
           "a canvas smaller than its own margin placed something");
    require(!orbitDiagramLayout(kEarthRadiusM, kDefaultOrbitM, 0.0, 24.5, 20.0, 4.5).valid,
           "a canvas with no room left for the dot placed something");
}

// The scale is the same whatever the canvas, so resizing the panel moves
// the picture and not the geometry.
void resizingScalesAndChangesNothingElse() {
    const auto small = orbitDiagramLayout(kEarthRadiusM, kDefaultOrbitM, 0.0, 96.0, 20.0, 4.5);
    const auto large = orbitDiagramLayout(kEarthRadiusM, kDefaultOrbitM, 0.0, 192.0, 20.0, 4.5);
    require(small.valid && large.valid, "a canvas size refused to place the default pair");
    const double factor = large.orbitRadiusPx / small.orbitRadiusPx;
    require(near(large.planetRadiusPx / small.planetRadiusPx, factor, 1e-9),
           "resizing stretched the world and the ring differently");
    require(near(large.installationY / small.installationY, factor, 1e-9),
           "resizing moved the dot to a different place on the face");
}
} // namespace

int main() {
    try {
        noonIsAtTwelveOClock();
        localMidnightIsAtSixOClock();
        quarterTurnsGoClockwiseRoundTheFace();
        oneScaleServesBothRadii();
        aLowOrbitRidesTheLimb();
        aFarOrbitLeavesTheWorldSmall();
        theDotStaysInsideTheFrame();
        theAngleIsTheEvaluatorsOwnSweep();
        theTiltMovesTheShadowNotTheRide();
        thePhaseOffsetMovesWhereTheDotOpens();
        whatTheSliderCallsEighteenOpensTheRunInTheShadow();
        theBandIsTheWorldsOwnShadow();
        aClearOrbitHasNoBand();
        theRampStraddlesTheGeometricEdge();
        aGrazingPassFloorsTheUmbraAtZero();
        theRampIsWidenedUntilItCanBeSeen();
        nonsenseIsRefusedRatherThanDrawn();
        resizingScalesAndChangesNothingElse();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "orbit diagram ok\n";
    return 0;
}
