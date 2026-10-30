#pragma once

#include <nsi/core/Installation.h>
#include <nsi/systems/CellAtmosphere.h>
#include <nsi/systems/StellarExposure.h>

namespace station::editor {
// A top-down look at the ride: the world in the middle, the Installation
// out on its ring, the star pinned at 12 o'clock.
//
// Nothing here recomputes orbital physics. The angle the Installation has
// travelled comes out of the evaluator's own star vector and the blocked
// fraction that colours it comes out of the same call that fed the hull,
// so the picture cannot part from what is being simulated. What is worked
// out in this file is only where a thing gets drawn.

// The layout in pixels, held apart from ImGui so the mapping is assertable
// without a window. Positions are relative to the planet centre in screen
// axes: +x right, +y down.
struct OrbitDiagramLayout {
    // False when nothing can be placed - a radius that is not a positive
    // number, an angle that is not a number, or a canvas with no room in
    // it. The caller says so out loud rather than drawing a circle of
    // radius zero and letting it pass for a world.
    bool valid{false};
    double planetRadiusPx{};
    double orbitRadiusPx{};
    double installationX{};
    double installationY{};
};

// How far the Installation has come since Local noon, read off the star
// vector the evaluator produced instead of off the clock a second time.
// The evaluator sweeps the star toward -x while the Installation travels
// along +x, and the axial tilt scales both in-plane components by
// cos(tilt), so the tilt cancels in the ratio: the position round the
// planet stays what it is however far the world leans, which is right,
// because leaning the world moves the shadow, not the ride.
double orbitAngleRadians(const nsi::systems::StellarExposure&);

// Fit the world and the ride into a square of the given half-width and
// set the Installation at angleRad clockwise from 12 o'clock, so the
// angle reads straight off a clock face: noon at the top, local midnight
// at the bottom, where the shadow is.
//
// One scale serves both radii. The distance from the planet centre is the
// authored distance against the authored world radius, so nothing is
// stretched relative to anything else and a low orbit genuinely rides the
// limb. The Installation itself is drawn at a fixed readable size, which
// is the only thing here not to scale and has to be: at true scale it
// would be sub-pixel.
OrbitDiagramLayout orbitDiagramLayout(double planetRadiusM, double orbitRadiusM,
                                     double angleRad, double canvasHalfPx,
                                     double marginPx, double installationRadiusPx);

// The penumbra is half a solar degree wide, which at any canvas a person
// actually looks at is a fraction of a pixel: drawn true it is not a
// band but a rounding error. So it gets a floor in pixels and the
// picture says how far it was stretched to reach it. The umbra is never
// touched - where totality starts and ends stays exactly where the model
// puts it - so only the ramp's outer edge moves, never its inner one.
// Returns the width to draw, never less than the true width.
double drawnPenumbraWidthDegrees(double trueWidthDeg, double orbitRadiusPx, double minPx);

// The panel section. elapsedSeconds is simulated time, so the dot moves
// with the run and sits at the authored opening position when the clock
// has not started. The runtime is optional: with one, the dot is coloured
// by the sky the hull is under; without one, by the authored record.
void drawOrbitDiagram(const nsi::core::Installation&,
                      const nsi::systems::CellAtmosphereTransport* runtime = nullptr,
                      double elapsedSeconds = 0.0);
} // namespace station::editor
