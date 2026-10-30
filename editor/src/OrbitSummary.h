#pragma once

#include <nsi/core/Installation.h>
#include <nsi/systems/CellAtmosphere.h>

#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace station::editor {
// The environment the author has written, read back beside what it implies.
// Every derived number here comes from the bridge's exposure evaluator -
// the single owner of every orbital formula - so the summary can never
// disagree with what is being simulated. Nothing in this file recomputes
// orbital physics.
//
// A derived field that has no sky behind it reads NaN rather than zero:
// an unavailable eclipse duration must not look like a sunlit orbit, and
// an unavailable drift must not look like Geostationary.
struct OrbitSummary {
    // An Orbit is authored and makes a sky the model can read.
    bool authored{};
    // A hand-authored solar profile owns the whole stellar term, so the
    // orbital sky is suppressed and nothing derived from it is live.
    bool overridden{};
    // Orbital period equals the planet's sidereal rotation. A displayed
    // condition only - it gates nothing.
    bool geostationary{};
    // The authored inputs, exactly as the document holds them.
    double stellarIrradianceWPerM2{};
    double orbitalPeriodS{};
    double radiusFromPlanetCentreM{};
    double axialTiltDeg{};
    double phaseOffsetPercent{};
    double albedo{};
    // The world being orbited. Defaults to the one constant this model
    // carried before the radius was authored, so a summary built from a
    // record that never set it still reads as Earth rather than zero.
    double planetRadiusM{nsi::core::PlanetRadiusM};
    // Derived from the authored inputs. NaN when nothing can be derived,
    // which is the default so a summary that never reached a sky cannot
    // report a plausible zero.
    double eclipseDurationS{std::numeric_limits<double>::quiet_NaN()};
    double sunlitDurationS{std::numeric_limits<double>::quiet_NaN()};
    double groundDriftDegPerDay{std::numeric_limits<double>::quiet_NaN()};
    // The thermal model and preset this environment is being read against,
    // so a changed physical default reads as a model change rather than as
    // a data migration the author somehow caused.
    std::string_view modelRevision{};
    std::string_view presetRevision{};
};

// The phase offset is stored as a percentage of the Orbital period but is
// authored and read as an angle round the cycle: 0 deg is Local noon and
// 180 deg is local midnight. The fold puts any stored value on the one
// turn the slider shows, because 370 deg and 10 deg genuinely do open
// the run in the same place. A percentage that is not a number folds to no
// angle at all rather than to a plausible one, so the readout says
// unavailable instead of quietly claiming noon.
double phaseAngleFromOffsetPercent(double percent);
// The inverse, for writing the slider back. A non-finite angle stays
// non-finite so core refuses the edit out loud instead of storing a zero
// that moved the run somewhere the author never set it.
double offsetPercentFromPhaseAngle(double degrees);

// The runtime is optional: with one, the summary reports the model and
// preset actually simulating and honours its solar-profile override. Without
// one it reads the authored document against the current revisions.
OrbitSummary orbitSummary(const nsi::core::Installation&,
                         const nsi::systems::CellAtmosphereTransport* runtime = nullptr);

// The summary as the panel's lines, built apart from any draw list so the
// badge and the derived readouts are assertable without ImGui. The badge
// line is present exactly when OrbitSummary::geostationary holds.
std::vector<std::string> orbitSummaryLines(const OrbitSummary&);

// The badge text, shared by the summary lines and anything else that wants
// to say the same thing the same way.
inline constexpr std::string_view kGeostationaryBadge = "Geostationary";

void drawOrbitSummary(const nsi::core::Installation&,
                      const nsi::systems::CellAtmosphereTransport* runtime = nullptr);
} // namespace station::editor
