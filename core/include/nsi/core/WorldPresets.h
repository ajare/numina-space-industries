#pragma once

#include <nsi/core/Installation.h>

#include <array>
#include <cstddef>
#include <string_view>

namespace nsi::core {
// One world an Installation can be authored to orbit, described in the
// four quantities this model actually reads. Values come from the NASA
// NSSDCA Planetary Fact Sheet - mean radius, Bond albedo, obliquity to
// orbit - with the irradiance derived as 1361 W/m^2 divided by the square
// of each world's mean distance from the Sun in AU.
//
// The Sun is deliberately not in this list, and cannot be. In this model
// the star and the orbited world are separate things: the star lights the
// scene, and the world occludes it, reflects it and re-radiates it. The
// Sun has no albedo to author and no shine to give that is not simply the
// star again, so there is no honest row for it. What every row here does
// carry is our Sun, at that world's distance - which is the only way the
// Sun appears in a catalogue of worlds at all.
struct WorldPreset {
    std::string_view name;
    // Stellar irradiance at the world's mean orbital distance, W/m^2.
    double stellarIrradianceWPerM2;
    // Bond albedo: the fraction of incident light the world returns to
    // space across all wavelengths and phase angles. It is what the model
    // reflects and, through energy balance, what it does not glow with.
    double albedo;
    // The model's axial tilt in degrees, already folded from the published
    // obliquity. See foldAxialTiltDegrees.
    double axialTiltDeg;
    // Mean radius in metres. The radius of the shadow cylinder.
    double planetRadiusM;
};

// Published obliquity is the angle between the spin axis and the orbit
// normal, and it runs past 90 degrees for the retrograde worlds - Venus
// at 177.4, Uranus tipped onto its side at 97.8. This model's tilt is
// the latitude the star sits directly over at solstice, which cannot
// leave (-90, 90), and it is indifferent to which way the world spins
// because rotation drives nothing in it. Past the pole the latitude comes
// back down the other side, so the fold is 180 minus the obliquity:
// 177.4 becomes 2.6 and 97.8 becomes 82.2.
constexpr double foldAxialTiltDegrees(double obliquityDeg) noexcept {
    return obliquityDeg <= 90.0 ? obliquityDeg : 180.0 - obliquityDeg;
}

// The eight planets, Sun outward. Each tilt is written as the published
// obliquity passed through the fold, so the published number is the only
// thing a reader has to check and nobody has to redo the subtraction by
// hand - which matters, because 180 - 177.4 is not the same double as
// 2.6 and a hand-folded table would not match itself on reload.
// Earth is also what every Orbit field defaults to, so the default sky is
// this row.
inline constexpr std::array<WorldPreset, 8> kWorldPresets{{
    // 0.387 AU, obliquity 0.034 deg - effectively none, but the real
    // number rather than a rounded one.
    {"Mercury", 9086.0, 0.088, foldAxialTiltDegrees(0.034), 2439700.0},
    // 0.723 AU, obliquity 177.4 deg: retrograde, nearly upright, folding
    // to a 2.6 deg solstice latitude.
    {"Venus", 2602.0, 0.760, foldAxialTiltDegrees(177.4), 6051800.0},
    // 1.000 AU, obliquity 23.4 deg.
    {"Earth", 1361.0, 0.306, foldAxialTiltDegrees(23.4), 6371000.0},
    // 1.524 AU, obliquity 25.2 deg.
    {"Mars", 586.0, 0.250, foldAxialTiltDegrees(25.2), 3389500.0},
    // 5.204 AU, obliquity 3.1 deg. The albedo is the 2018 Cassini
    // revision, which returned about a third more light to space than the
    // figure it replaced and left Jupiter cooler than the old number did.
    {"Jupiter", 50.3, 0.503, foldAxialTiltDegrees(3.1), 69911000.0},
    // 9.572 AU, obliquity 26.7 deg.
    {"Saturn", 14.9, 0.342, foldAxialTiltDegrees(26.7), 58232000.0},
    // 19.16 AU, obliquity 97.8 deg: tipped onto its side, folding to
    // 82.2 deg - a star that spends solstice almost over the pole.
    {"Uranus", 3.7, 0.300, foldAxialTiltDegrees(97.8), 25362000.0},
    // 30.18 AU, obliquity 28.3 deg.
    {"Neptune", 1.5, 0.290, foldAxialTiltDegrees(28.3), 24622000.0},
}};

// Returned when no listed world matches.
inline constexpr std::size_t kNoWorldPreset = static_cast<std::size_t>(-1);

// Which listed world an Orbit's authored fields describe, or
// kNoWorldPreset. The comparison is exact on all four: a preset applied
// whole matches itself exactly, and the smallest hand edit moves the
// record to Custom. That is the honest reading - a nudged albedo is not
// Jupiter any more - and it declines to invent a tolerance that would have
// to be right for a gas giant's albedo and a Mercurian one at once.
std::size_t worldPresetMatching(const Installation::Orbit&);

// Move an Orbit onto a listed world. The world fields are set from the
// preset and the ride is left alone: the Orbital period and the phase
// offset stay exactly as authored, because which world you are about does
// not decide how fast you travel around it or where in the cycle you
// open. Throws std::out_of_range for an index past the catalogue.
//
// One thing has to follow. A Hull cannot be inside the world it orbits, so
// if the new world would swallow the current orbit the radius moves out
// with the ground and keeps the altitude above the surface that the author
// already had. Refusing instead would leave them unpicking a radius by
// hand to arrive back at the number they started from.
Installation::Orbit applyWorldPreset(const Installation::Orbit&, std::size_t index);
} // namespace nsi::core
