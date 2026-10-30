#pragma once

#include <nsi/core/Installation.h>
#include <nsi/core/Units.h>
#include <array>

namespace nsi::systems {

// The stellar environment at one instant, expressed in the Installation body
// frame. Nadir lock fixes that frame: +z is zenith (away from the planet
// centre, which is already the Level axis), +x is along-track and +y is
// cross-track, which is also the orbit normal and therefore fixed.
struct StellarExposure {
    // Unit vector from the Installation toward the star. All zero when there
    // is no star to see, so a dot product against it is never positive.
    std::array<double, 3> sunDirection{0.0, 0.0, 1.0};
    // 0 in full sun, 1 in total eclipse.
    double eclipse{0.0};
    bool sunlit{true};
};

// The single owner of every orbital formula in this project. Pure, with no
// state beyond what its constructor was given, and allocation-free on every
// path so it can sit on the per-step hot path. It knows no Installation
// geometry, no solver and no Editor: it turns authored numbers and simulated
// seconds into a sky. No other module may recompute these.
//
// See ADR-0008: the planet's rotation drives nothing, so it is not an input.
class StellarExposureEvaluator {
public:
    // Beta is authored on the Orbit record itself: the angle between the
    // star and the orbital plane, in radians. It is what tilts a constant
    // cross-track component of sin(beta) into the sun direction, shallows
    // the eclipse, and decides whether there is any eclipse at all.
    // No authored Orbit: a dark sky that answers zero to every flux query,
    // including Planet shine, and leaves the Deep-space sink as it was given.
    StellarExposureEvaluator() noexcept = default;
    explicit StellarExposureEvaluator(const core::Installation::Orbit& orbit) noexcept;

    // Dark means the authored numbers cannot produce a sky: every flux query
    // then answers zero.
    bool dark() const noexcept { return dark_; }
    double stellarIrradianceWPerM2() const noexcept { return irradianceWPerM2_; }
    // Fraction of one Orbital period spent inside the planet's shadow. It
    // depends on the radius and Beta alone, never on the phase offset: the
    // offset moves the eclipse along the clock, it does not resize it.
    double eclipseFraction() const noexcept { return eclipseFraction_; }
    // The authored Beta angle and phase offset, as the record carried them.
    double betaAngleRadians() const noexcept { return betaRadians_; }
    double phaseOffsetSeconds() const noexcept { return phaseOffsetSeconds_; }

    // elapsedSeconds is simulated time since Local noon, which is t = 0
    // while the authored phase offset is zero. A non-zero offset shifts the
    // whole profile: the exposure at elapsed t under an offset o is the
    // exposure at t + o with no offset.
    StellarExposure evaluate(double elapsedSeconds) const noexcept;

    // Absorbed direct stellar flux in W/m^2 for one Structural surface with
    // the given outward unit normal and solar absorptivity:
    // max(0, n . s) * alpha * irradiance * (1 - eclipse). Never negative: a
    // surface turned away from the star receives nothing, which is what
    // keeps at most half the hull lit.
    //
    // One gate rides on top of the cosine: a surface whose normal points
    // below the local horizon (n.z < 0) receives nothing while the star is
    // at or below that horizon. The planet is treated as filling the whole
    // below-horizon sky for a surface that faces it, so the nadir-facing
    // underside never takes direct sunlight at any Beta and Planet shine is
    // the only external heat down there (ADR-0008). A real point element
    // would still catch a grazing beam in the thin annulus just outside the
    // planet's limb; this model deliberately collapses it, because an
    // author reading their nadir hull as sunlit would be reading a lie
    // about what the underside is ever exposed to.
    double directFluxWPerM2(const std::array<double, 3>& outwardNormal,
                            double absorptivity,
                            const StellarExposure& exposure) const noexcept;

    // Planet shine: everything the hull receives from the planet's disc
    // rather than from the star directly. Two components with two different
    // fates in Eclipse, which is why they are never added together here.

    // The planet's own infrared temperature, derived by global energy
    // balance as (irradiance * (1 - albedo) / 4 sigma)^(1/4) and never
    // authored. A dimmer star or a more reflective planet both leave less to
    // radiate, so a dead star never leaves a glowing world behind.
    double planetInfraredTemperatureK() const noexcept { return planetTemperatureK_; }
    double albedo() const noexcept { return albedo_; }

    // View factor from one planar Structural surface to the planet, the
    // element-to-sphere result (Rp/r)^2 * max(0, -n . z). A nadir face at
    // ISS altitude sees 0.880 of its sky; a zenith or vertical face sees
    // none. This is deliberately not the sky-coverage fraction 1 - cos a,
    // which is ~35% smaller at that altitude and would understate the load.
    double planetViewFactor(const std::array<double, 3>& outwardNormal) const noexcept;

    // Planetary infrared arriving at one surface, W/m^2: view * sigma *
    // T_planet^4. Unaffected by Eclipse, because the ground beneath an
    // eclipsed station is the planet's own shadowed side, still warm.
    double planetInfraredFluxWPerM2(const std::array<double, 3>& outwardNormal) const noexcept;

    // Sunlight bounced off the planet's disc, W/m^2: view * albedo *
    // irradiance, and exactly zero while eclipsed. There is no sunlight in
    // the shadow to reflect, so without this gate a station would absorb
    // hundreds of watts while sitting in total darkness.
    double reflectedSunlightFluxWPerM2(const std::array<double, 3>& outwardNormal,
                                      const StellarExposure& exposure) const noexcept;

    // The Deep-space sink a surface with this view factor actually radiates
    // toward: [(1 - F) * T_space^4 + F * T_planet^4]^(1/4). A grey body
    // absorbs planetary infrared with its emissivity, the same coefficient
    // with which it emits, so folding the planet into the sink temperature
    // is exact rather than an approximation and needs no new solver input.
    // F = 0 returns the bare Deep-space sink unchanged.
    double effectiveSinkTemperatureK(double spaceTemperatureK, double viewFactor) const noexcept;

private:
    double irradianceWPerM2_{};
    double periodSeconds_{};
    double albedo_{};
    double betaRadians_{};
    double phaseOffsetSeconds_{};
    // The phase offset expressed in turns of the orbit and already folded
    // into [0, 1), so evaluate() only has to fold the elapsed turns and
    // add two numbers of order one.
    double phaseTurns_{};
    double planetTemperatureK_{};
    // (Rp/r)^2, the view factor of a surface facing the planet outright.
    double planetViewScale_{};
    double eclipseFraction_{};
    // The zenith cosine below which the star is hidden: -sqrt(1 - (Rp/r)^2).
    // A fixed property of the shadow cylinder and the orbit radius, not of
    // Beta: Beta decides how far down the star ever gets, and therefore
    // whether it ever reaches this line at all.
    double umbraZenithCosine_{-2.0};
    double sunCrossTrack_{};     // sin(beta), constant through the orbit
    double sunPlaneScale_{1.0};  // cos(beta), the in-plane share of the sweep
    bool dark_{true};
};

} // namespace nsi::systems
