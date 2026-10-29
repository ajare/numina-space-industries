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
    // Beta is fixed at zero for this slice. The parameter keeps the geometry
    // written as real geometry rather than a hard-coded coplanar sun, and
    // defaults to the worst-case eclipse the model wants until Beta is
    // authored in its own slice.
    // No authored Orbit: a dark sky that answers zero to every flux query.
    StellarExposureEvaluator() noexcept = default;
    explicit StellarExposureEvaluator(const core::Installation::Orbit& orbit,
                                     double betaRadians = 0.0) noexcept;

    // Dark means the authored numbers cannot produce a sky: every flux query
    // then answers zero.
    bool dark() const noexcept { return dark_; }
    double stellarIrradianceWPerM2() const noexcept { return irradianceWPerM2_; }
    // Fraction of one Orbital period spent inside the planet's shadow. At
    // Beta = 0 this is asin(Rp/r)/pi.
    double eclipseFraction() const noexcept { return eclipseFraction_; }

    // elapsedSeconds is simulated time since Local noon, which is t = 0 with
    // the sunlit arc centred on it.
    StellarExposure evaluate(double elapsedSeconds) const noexcept;

    // Absorbed direct stellar flux in W/m^2 for one Structural surface with
    // the given outward unit normal and solar absorptivity:
    // max(0, n . s) * alpha * irradiance * (1 - eclipse). Never negative: a
    // surface turned away from the star receives nothing, which is what
    // keeps at most half the hull lit.
    double directFluxWPerM2(const std::array<double, 3>& outwardNormal,
                            double absorptivity,
                            const StellarExposure& exposure) const noexcept;

private:
    double irradianceWPerM2_{};
    double periodSeconds_{};
    double eclipseFraction_{};
    // Zenith cosine below which the star is hidden: -k, with
    // k = sqrt(1 - (Rp/r)^2) / cos(beta). Below -1 the shadow is never
    // reached and there is no eclipse at all.
    double umbraZenithCosine_{-2.0};
    double sunCrossTrack_{};     // sin(beta), constant through the orbit
    double sunPlaneScale_{1.0};  // cos(beta), the in-plane share of the sweep
    bool dark_{true};
};

} // namespace nsi::systems
