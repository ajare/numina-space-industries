#include "OrbitDiagram.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace station::editor {
namespace {
constexpr double kPi{3.14159265358979323846};

// The canvas. It takes the width the panel offers and stops short of
// getting silly, because past this the world is a disc you cannot see the
// dot move against.
constexpr float kMinCanvasPx{168.0f};
constexpr float kMaxCanvasPx{256.0f};
// Room left at the top for the star marker, all the way round so the ring
// never touches the frame either.
constexpr float kMarginPx{20.0f};
// The Installation is drawn at a fixed size. At true scale it would be
// well under a pixel, and a dot you cannot find is not a readout.
constexpr float kInstallationRadiusPx{4.5f};

// The world reads as ground and the ring as the path, so neither of them
// competes with the one thing that moves.
constexpr ImU32 kPlanetColour{IM_COL32(72, 96, 118, 255)};
constexpr ImU32 kPlanetRimColour{IM_COL32(122, 150, 174, 255)};
constexpr ImU32 kOrbitColour{IM_COL32(146, 158, 174, 110)};
// The umbra is the shadow that means something. Shade, not data, the
// same reading the exposure trace gives its Eclipse band.
constexpr ImU32 kUmbraColour{IM_COL32(14, 24, 46, 172)};
// The penumbra fades out from the umbra's edge in strips, because the
// model's eclipse is a linear ramp and a hard edge would describe a
// shadow that snaps from full to none.
constexpr int kPenumbraRed{20};
constexpr int kPenumbraGreen{32};
constexpr int kPenumbraBlue{56};
constexpr int kPenumbraInnerAlpha{118};
constexpr int kPenumbraOuterAlpha{20};
constexpr int kPenumbraSteps{5};
// The floor the penumbra is widened to so that it can be seen. Half a
// solar degree is a fraction of a pixel at any canvas a person looks at.
constexpr float kMinPenumbraPx{7.0f};
constexpr ImU32 kStarColour{IM_COL32(246, 214, 122, 255)};
constexpr ImU32 kDotOutlineColour{IM_COL32(10, 14, 20, 210)};

// Sunlit through to eclipsed, with the penumbra falling where it falls by
// itself: the ramp is already in the blocked fraction, so nothing here
// invents an edge the model does not have.
ImU32 dotColour(double eclipse) {
    constexpr float sunlit[4]{0.95f, 0.80f, 0.38f, 1.0f};
    constexpr float shadowed[4]{0.32f, 0.44f, 0.68f, 1.0f};
    const float t = static_cast<float>(std::clamp(eclipse, 0.0, 1.0));
    int channel[4];
    for (std::size_t i = 0; i < 4; ++i) {
        const float mixed = std::clamp(sunlit[i] + (shadowed[i] - sunlit[i]) * t, 0.0f, 1.0f);
        channel[i] = static_cast<int>(mixed * 255.0f + 0.5f);
    }
    return IM_COL32(channel[0], channel[1], channel[2], channel[3]);
}
} // namespace

double orbitAngleRadians(const nsi::systems::StellarExposure& exposure) {
    // The evaluator puts the star at {-cos(tilt) * sin(phase), sin(tilt),
    // cos(tilt) * cos(phase)} in the body frame, sweeping it toward -x as
    // the Installation travels along +x. Reading the angle back out of that
    // pair gives the phase straight, and the cos(tilt) rides on both terms
    // so it cancels: leaning the world moves the shadow, not the ride.
    return std::atan2(-exposure.sunDirection[0], exposure.sunDirection[2]);
}

OrbitDiagramLayout orbitDiagramLayout(double planetRadiusM, double orbitRadiusM, double angleRad,
                                     double canvasHalfPx, double marginPx,
                                     double installationRadiusPx) {
    OrbitDiagramLayout layout;
    if (!std::isfinite(planetRadiusM) || !std::isfinite(orbitRadiusM) || !std::isfinite(angleRad))
        return layout;
    if (!(planetRadiusM > 0.0) || !(orbitRadiusM > 0.0)) return layout;
    // The ring is the thing that has to fit, so the scale comes off it and
    // the world takes the very same scale. Nothing is stretched against
    // anything else, which is what makes a low orbit look like what it is:
    // a ride along the limb.
    const double usable = canvasHalfPx - marginPx - installationRadiusPx;
    if (!(usable > 0.0)) return layout;
    const double scale = usable / orbitRadiusM;
    if (!std::isfinite(scale) || !(scale > 0.0)) return layout;
    layout.valid = true;
    layout.orbitRadiusPx = usable;
    layout.planetRadiusPx = planetRadiusM * scale;
    // Clockwise from 12 o'clock, so the angle reads straight off a clock
    // face and the phase offset the author set is the position the dot
    // opens on: noon at the top, local midnight at the bottom, in the
    // shadow.
    layout.installationX = layout.orbitRadiusPx * std::sin(angleRad);
    layout.installationY = -layout.orbitRadiusPx * std::cos(angleRad);
    return layout;
}

double drawnPenumbraWidthDegrees(double trueWidthDeg, double orbitRadiusPx, double minPx) {
    if (!std::isfinite(trueWidthDeg) || trueWidthDeg <= 0.0) return 0.0;
    if (!std::isfinite(orbitRadiusPx) || !(orbitRadiusPx > 0.0)) return trueWidthDeg;
    if (!(minPx > 0.0)) return trueWidthDeg;
    // An arc of minPx at the ring's radius, turned back into degrees.
    const double floorDeg = minPx / orbitRadiusPx * 180.0 / kPi;
    return std::max(trueWidthDeg, floorDeg);
}

void drawOrbitDiagram(const nsi::core::Installation& installation,
                     const nsi::systems::CellAtmosphereTransport* runtime, double elapsedSeconds) {
    if (!ImGui::TreeNodeEx("Orbit diagram", ImGuiTreeNodeFlags_DefaultOpen)) return;

    const auto orbit = installation.orbit();
    if (!orbit) {
        ImGui::TextWrapped("No Orbit authored: no world to draw and no ring to ride.");
        ImGui::TreePop();
        return;
    }

    // Prefer the evaluator the hull is actually under, so the dot is
    // coloured with the sky being simulated rather than the one sitting on
    // the document. A hand-authored solar profile leaves that evaluator
    // dark by design - the orbital model was never built - so the picture
    // falls back to the authored record and then says plainly that it is
    // showing a sky the hull is not receiving.
    const bool overridden = runtime && runtime->solarCycleOverrides();
    std::optional<nsi::systems::StellarExposureEvaluator> built;
    const nsi::systems::StellarExposureEvaluator* evaluator = nullptr;
    if (runtime && !overridden) evaluator = &runtime->exposureEvaluator();
    else {
        built.emplace(*orbit);
        evaluator = &*built;
    }
    if (evaluator->dark()) {
        ImGui::TextWrapped("The authored numbers make no sky, so there is nothing to place.");
        ImGui::TreePop();
        return;
    }

    const auto exposure = evaluator->evaluate(elapsedSeconds);

    const float canvas =
        std::clamp(ImGui::GetContentRegionAvail().x, kMinCanvasPx, kMaxCanvasPx);
    const auto layout =
        orbitDiagramLayout(orbit->planetRadiusM, orbit->orbitalRadiusM(),
                          orbitAngleRadians(exposure), canvas / 2.0f, kMarginPx,
                          kInstallationRadiusPx);
    if (!layout.valid) {
        ImGui::TextWrapped("The panel is too narrow to draw the orbit in.");
        ImGui::TreePop();
        return;
    }

    // The shadow's shape comes off the evaluator rather than being
    // worked out here: the umbra's half-width, and the outer edge of the
    // ramp that wraps it, both measured from the point directly away
    // from the star. On a grazing pass the umbra floors at zero and the
    // whole band is ramp, which is what the model does with it.
    //
    // A tilt that carries the ring clear of the shadow leaves nothing to
    // draw. Asked for an outer edge anyway the ramp would still hand back
    // half a solar degree of band around a shadow that does not exist,
    // so the whole band is gated on there being a shadow at all.
    const bool hasShadow = evaluator->eclipseFraction() > 0.0;
    const double umbraHalfDeg = hasShadow ? evaluator->umbraHalfWidthDegrees() : 0.0;
    const double truePenumbraDeg =
        hasShadow ? evaluator->penumbraOuterHalfWidthDegrees() - umbraHalfDeg : 0.0;
    const double penumbraDeg =
        drawnPenumbraWidthDegrees(truePenumbraDeg, layout.orbitRadiusPx, kMinPenumbraPx);
    const double exaggeration =
        truePenumbraDeg > 0.0 ? penumbraDeg / truePenumbraDeg : 1.0;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(canvas, canvas));
    const ImVec2 centre{origin.x + canvas * 0.5f, origin.y + canvas * 0.5f};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float outer = canvas * 0.5f;

    // Screen angles run from +x toward +y, so straight down - the point
    // away from a star at 12 o'clock - is half pi. A wedge and its mirror
    // are drawn together so a band reads as one shape across the face
    // rather than two halves that have to be kept straight.
    const float down = static_cast<float>(kPi * 0.5);
    auto wedge = [&](double fromDeg, double toDeg, ImU32 colour) {
        const float inner = static_cast<float>(fromDeg * kPi / 180.0);
        const float span = static_cast<float>(toDeg * kPi / 180.0);
        draw->PathClear();
        draw->PathLineTo(centre);
        draw->PathArcTo(centre, outer, down - span, down - inner);
        draw->PathFillConvex(colour);
        draw->PathClear();
        draw->PathLineTo(centre);
        draw->PathArcTo(centre, outer, down + inner, down + span);
        draw->PathFillConvex(colour);
    };

    // The penumbra first, and as a ramp rather than a step: strips
    // fading out from the umbra edge, each lighter than the last. The
    // model smooths each shadow edge over one solar diameter instead of
    // dropping the light all at once, and a hard-edged band here would
    // describe the thing the model went to some trouble not to do.
    if (penumbraDeg > 0.0) {
        for (int step = 0; step < kPenumbraSteps; ++step) {
            const double from = umbraHalfDeg + penumbraDeg * step / kPenumbraSteps;
            const double to = umbraHalfDeg + penumbraDeg * (step + 1) / kPenumbraSteps;
            const double t = (step + 0.5) / kPenumbraSteps;
            const int alpha = static_cast<int>(
                kPenumbraInnerAlpha + (kPenumbraOuterAlpha - kPenumbraInnerAlpha) * t + 0.5);
            wedge(from, to,
                 IM_COL32(kPenumbraRed, kPenumbraGreen, kPenumbraBlue, alpha));
        }
    }
    // The umbra over the top of it: the part where the star is gone
    // entirely rather than merely dimming.
    if (umbraHalfDeg > 0.0) wedge(0.0, umbraHalfDeg, kUmbraColour);

    draw->AddCircleFilled(centre, static_cast<float>(layout.planetRadiusPx), kPlanetColour);
    draw->AddCircle(centre, static_cast<float>(layout.planetRadiusPx), kPlanetRimColour, 96, 1.0f);
    draw->AddCircle(centre, static_cast<float>(layout.orbitRadiusPx), kOrbitColour, 128, 1.0f);

    // The star, pinned above the ring. It never moves because nothing in
    // this model moves it: the whole sky turns on the ride, not on the
    // star going somewhere.
    const float starTop = origin.y + 2.0f;
    draw->AddTriangleFilled({centre.x - 7.0f, starTop}, {centre.x + 7.0f, starTop},
                           {centre.x, starTop + 11.0f}, kStarColour);

    const ImVec2 dot{centre.x + static_cast<float>(layout.installationX),
                    centre.y + static_cast<float>(layout.installationY)};
    // Outlined first so the dot stays findable where it overlaps the
    // world, which at a low orbit is everywhere.
    draw->AddCircleFilled(dot, kInstallationRadiusPx + 1.0f, kDotOutlineColour);
    draw->AddCircleFilled(dot, kInstallationRadiusPx, dotColour(exposure.eclipse));

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Star at 12 o'clock, Local noon at the top.\n"
                         "Umbra: +/- %.2f deg about the far point.\n"
                         "Penumbra: %.2f deg of ramp beyond it,\n"
                         "  each side, drawn %.1fx wider than true -\n"
                         "  at its real width it is under a pixel of\n"
                         "  this canvas. The dot is never exaggerated.\n"
                         "Blocked right now: %.1f%%",
                         umbraHalfDeg, truePenumbraDeg, exaggeration, exposure.eclipse * 100.0);
    if (overridden)
        ImGui::TextWrapped("A hand-authored solar profile owns the stellar term: this is the "
                          "authored orbital sky, not what the hull is receiving.");
    ImGui::TreePop();
}

} // namespace station::editor
