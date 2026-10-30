#pragma once

#include <nsi/core/Installation.h>
#include <nsi/core/Units.h>
#include <array>
#include <limits>

namespace nsi::systems {

// The stellar environment at one instant, expressed in the Installation body
// frame. Nadir lock fixes that frame: +z is zenith (away from the planet
// centre, which is already the Level axis), +x is along-track and +y is
// cross-track, which is also the orbit normal and therefore fixed.
struct StellarExposure {
    // Unit vector from the Installation toward the star. All zero when there
    // is no star to see, so a dot product against it is never positive.
    std::array<double, 3> sunDirection{0.0, 0.0, 1.0};
    // Blocked stellar fraction: 0 in full sun, 1 in total eclipse, linear
    // penumbra ramps between. Each edge spans 0.533 * period / 360 seconds,
    // centred on the geometric crossing. Overlapping edges in a grazing
    // eclipse reduce the peak continuously rather than forcing totality.
    double eclipse{0.0};
    // Any direct stellar disc remains visible (eclipse < 1), including
    // penumbra. Reflected light uses 1 - eclipse, not this boolean weight.
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
    // The axial tilt is authored on the Orbit record itself, in degrees:
    // the planet leans this far toward the star while the Installation rides
    // its equator, which is the same thing as the star sitting this far out
    // of the orbital plane. It is what tilts a constant cross-track share
    // of the star into the sky, shallows the eclipse, and decides whether
    // there is any eclipse at all.
    // No authored Orbit: a dark sky that answers zero to every flux query,
    // including Planet shine, and leaves the Deep-space sink as it was given.
    StellarExposureEvaluator() noexcept = default;
    explicit StellarExposureEvaluator(const core::Installation::Orbit& orbit) noexcept;

    // Dark means the authored numbers cannot produce a sky: every flux query
    // then answers zero.
    bool dark() const noexcept { return dark_; }
    double stellarIrradianceWPerM2() const noexcept { return irradianceWPerM2_; }
    // Geometric shadow fraction, also the orbit-average blocked fraction
    // after penumbra smoothing (not the time spent in total eclipse). It
    // depends on the radius and the tilt alone, never on the phase offset:
    // the offset moves the eclipse along the clock, it does not resize it.
    double eclipseFraction() const noexcept { return eclipseFraction_; }
    // The authored axial tilt (degrees) and phase offset (percent of the
    // Orbital period), as the record carried them.
    double axialTiltDegrees() const noexcept { return axialTiltDeg_; }
    double phaseOffsetPercent() const noexcept { return phaseOffsetPercent_; }
    // The same offset in seconds, for a readout that wants the clock value
    // rather than the position on the cycle. Unavailable with no sky, since
    // there is no period to take a percentage of.
    double phaseOffsetSeconds() const noexcept {
        return dark_ ? kUnavailable : phaseOffsetPercent_ / 100.0 * periodSeconds_;
    }
    // The authored Orbital period and orbital radius, as the record carried
    // them. Unavailable with no sky, so a summary cannot print a period of
    // zero for an Installation that has no orbit at all.
    double orbitalPeriodSeconds() const noexcept { return dark_ ? kUnavailable : periodSeconds_; }
    double radiusFromPlanetCentreMeters() const noexcept {
        return dark_ ? kUnavailable : radiusMeters_;
    }
    // The authored world's radius. This is the radius of the shadow
    // cylinder, so it is the number that decides how much of the orbit
    // falls into eclipse - exposed for readouts, never for a second
    // opinion about the geometry.
    double planetRadiusMeters() const noexcept {
        return dark_ ? kUnavailable : planetRadiusM_;
    }

    // Equivalent full-eclipse and full-sun durations: integrals of eclipse
    // and (1 - eclipse) over one orbit, not boolean sunlit/totality durations.
    // Centred penumbra ramps preserve the geometric shadow integral, even
    // when they overlap. These sum to the period and ignore phase offset.
    double eclipseDurationSeconds() const noexcept {
        return dark_ ? kUnavailable : eclipseFraction_ * periodSeconds_;
    }
    double sunlitDurationSeconds() const noexcept {
        return dark_ ? kUnavailable : periodSeconds_ - eclipseFraction_ * periodSeconds_;
    }

    // Ground drift: the apparent motion of the Installation's sub-point
    // across the surface, degrees per day. The sub-point rides with the
    // Installation at 360 degrees per Orbital period while the ground turns
    // beneath it at 360 degrees per sidereal rotation; the residue is what
    // an author on the surface would watch. Positive means the Installation
    // outruns the surface and its sub-point creeps east, negative means the
    // surface outruns it. Exactly zero at Geostationary, which is the only
    // reason to show it. This is a readout, not a thermal input: ADR-0008
    // keeps the planet's rotation out of the exposure model entirely.
    double groundDriftDegreesPerDay() const noexcept;

    // The Geostationary condition: Orbital period equal to the planet's
    // sidereal rotation. A displayed condition and never a mode - it gates
    // nothing, changes nothing, and disappears the moment the two periods
    // part.
    bool geostationary() const noexcept;

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
    // underside never takes direct sunlight at any tilt and Planet shine is
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
    // irradiance * (1 - eclipse), ramping through penumbra and exactly zero
    // in total eclipse. There is no sunlight in
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
    // What a derived quantity answers when there is no sky to derive it
    // from. Never zero: a zero eclipse duration would read as a permanently
    // sunlit orbit and a zero drift as Geostationary, both of which are
    // claims about a world this Installation has not authored.
    static constexpr double kUnavailable = std::numeric_limits<double>::quiet_NaN();

    double irradianceWPerM2_{};
    double periodSeconds_{};
    double radiusMeters_{};
    // The authored world's radius, the Rp in every (Rp/r)^2 below.
    double planetRadiusM_{};
    double albedo_{};
    double axialTiltDeg_{};
    // The tilt in radians, which is what the sweep is actually built from.
    double tiltRadians_{};
    double phaseOffsetPercent_{};
    // The phase offset expressed in turns of the orbit and already folded
    // into [0, 1), so evaluate() only has to fold the elapsed turns and
    // add two numbers of order one.
    double phaseTurns_{};
    double planetTemperatureK_{};
    // (Rp/r)^2, the view factor of a surface facing the planet outright.
    double planetViewScale_{};
    double eclipseFraction_{};
    double sunCrossTrack_{};     // sin(tilt), constant through the orbit
    double sunPlaneScale_{1.0};  // cos(tilt), the in-plane share of the sweep
    bool dark_{true};
};

} // namespace nsi::systems
