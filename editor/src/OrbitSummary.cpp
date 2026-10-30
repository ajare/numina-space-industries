#include "OrbitSummary.h"

#include "imgui.h"

#include <nsi/core/ThermalPresets.h>
#include <nsi/systems/StellarExposure.h>
#include <station/ThermalBody.h>

#include <cmath>
#include <cstdio>
#include <optional>

namespace station::editor {
namespace {
// Wrap a value into [0, cycle) so a whole turn either way reads as the
// same point. The remainder is taken first and only pushed forward when
// it came back negative: adding a cycle up front and then taking the
// remainder rounds the value on the way through, so 33.333% would come
// back as a slightly different 33.333% and the dial would drift every
// time the slider was drawn.
double foldToFirstTurn(double value, double cycle) {
    double folded = std::fmod(value, cycle);
    if (folded < 0.0) folded += cycle;
    return folded;
}

// One labelled reading. An unavailable value says so out loud rather than
// printing anything: a zero eclipse duration reads as a sunlit orbit and a
// zero drift reads as Geostationary, and neither is a fact about a sky the
// author has not authored.
template <typename... Converted>
std::string reading(std::string_view label, double value, const char* format,
                    Converted... converted) {
    std::string text(label);
    text += "  ";
    if (std::isfinite(value)) {
        char buffer[96];
        std::snprintf(buffer, sizeof buffer, format, value, converted...);
        text += buffer;
    } else {
        text += "unavailable";
    }
    return text;
}

std::string revisionLine(const OrbitSummary& summary) {
    std::string text = "Thermal model ";
    text += summary.modelRevision;
    text += " | preset ";
    text += summary.presetRevision;
    return text;
}
// The phase offset reads as the angle the slider shows, with the seconds
// it corresponds to in brackets - the same fact read off the clock, and
// only worth printing when there is a real period to take a fraction of.
// Otherwise it would name a duration this sky does not have.
std::string phaseReading(double percent, double periodSeconds) {
    const double angle = phaseAngleFromOffsetPercent(percent);
    std::string text = reading("Phase offset", angle, "%.1f deg of orbit");
    if (std::isfinite(angle) && std::isfinite(periodSeconds)) {
        char buffer[48];
        std::snprintf(buffer, sizeof buffer, " (%.1f s)", angle / 360.0 * periodSeconds);
        text += buffer;
    }
    return text;
}

std::string unavailableReadings(const OrbitSummary& summary) {
    if (summary.overridden)
        return "Derived values unavailable: a hand-authored solar profile owns the "
               "stellar term, so the orbital sky is suppressed";
    return "Derived values unavailable: the authored numbers cannot make a sky";
}
} // namespace

double phaseAngleFromOffsetPercent(double percent) {
    if (!std::isfinite(percent)) return std::numeric_limits<double>::quiet_NaN();
    // Fold to one turn before scaling, and scale by 360 over 100 rather
    // than by 3.6: 3.6 is not representable in binary, so a value pushed
    // through it would not come back the same number going the other way.
    return foldToFirstTurn(percent, 100.0) * 360.0 / 100.0;
}

double offsetPercentFromPhaseAngle(double degrees) {
    if (!std::isfinite(degrees)) return std::numeric_limits<double>::quiet_NaN();
    return foldToFirstTurn(degrees, 360.0) * 100.0 / 360.0;
}

OrbitSummary orbitSummary(const nsi::core::Installation& installation,
                         const nsi::systems::CellAtmosphereTransport* runtime) {
    OrbitSummary summary;
    // The revisions come off the running model when there is one, so the
    // summary is labelled with what is actually simulating rather than what
    // this build happens to default to.
    summary.modelRevision = runtime ? runtime->structure().modelRevision
                                  : station::NativeThermalModelRevision;
    summary.presetRevision = runtime ? runtime->structure().presetRevision
                                   : nsi::core::NativeThermalPresetRevision;
    const auto orbit = installation.orbit();
    summary.authored = orbit.has_value();
    if (!orbit) return summary;
    summary.stellarIrradianceWPerM2 = orbit->stellarIrradianceWPerM2;
    summary.orbitalPeriodS = orbit->orbitalPeriodS;
    summary.radiusFromPlanetCentreM = orbit->radiusFromPlanetCentreM;
    summary.axialTiltDeg = orbit->axialTiltDeg;
    summary.phaseOffsetPercent = orbit->phaseOffsetPercent;
    summary.albedo = orbit->albedo;
    summary.planetRadiusM = orbit->planetRadiusM;
    summary.overridden = runtime && runtime->solarCycleOverrides();

    // Read the derived numbers off the evaluator that is actually running
    // where there is one, so the summary cannot drift from the sky the hull
    // is under; otherwise off an evaluator built from the same record,
    // which is the same pure function of the same authored numbers.
    std::optional<nsi::systems::StellarExposureEvaluator> built;
    const nsi::systems::StellarExposureEvaluator* evaluator = nullptr;
    if (runtime) evaluator = &runtime->exposureEvaluator();
    else {
        built.emplace(*orbit);
        evaluator = &*built;
    }
    // Under a hand-authored profile the running evaluator is deliberately
    // dark - the orbital model was never built - and reporting derived
    // eclipse timings there would describe a sky that is not being flown.
    if (summary.overridden || evaluator->dark()) return summary;
    summary.eclipseDurationS = evaluator->eclipseDurationSeconds();
    summary.sunlitDurationS = evaluator->sunlitDurationSeconds();
    summary.groundDriftDegPerDay = evaluator->groundDriftDegreesPerDay();
    summary.geostationary = evaluator->geostationary();
    return summary;
}

std::vector<std::string> orbitSummaryLines(const OrbitSummary& summary) {
    std::vector<std::string> lines;
    if (!summary.authored) {
        lines.emplace_back("No Orbit authored: dark sky, zero stellar gain");
        lines.push_back(revisionLine(summary));
        return lines;
    }
    // The authored numbers in the two groups the panels put them in: the
    // world and how it answers the star, then the ride around it.
    lines.push_back(reading("Stellar irradiance", summary.stellarIrradianceWPerM2, "%.1f W/m^2"));
    lines.push_back(reading("Albedo", summary.albedo, "%.3f"));
    lines.push_back(reading("Axial tilt", summary.axialTiltDeg, "%+.2f deg"));
    lines.push_back(reading("Planet radius", summary.planetRadiusM, "%.0f m (%.0f km)",
                            summary.planetRadiusM / 1000.0));
    lines.push_back(reading("Orbital period", summary.orbitalPeriodS, "%.1f s (%.1f min)",
                            summary.orbitalPeriodS / 60.0));
    lines.push_back(reading("Orbital radius", summary.radiusFromPlanetCentreM, "%.0f m (%.0f km)",
                            summary.radiusFromPlanetCentreM / 1000.0));
    lines.push_back(phaseReading(summary.phaseOffsetPercent, summary.orbitalPeriodS));
    if (!std::isfinite(summary.eclipseDurationS) || !std::isfinite(summary.sunlitDurationS)
        || !std::isfinite(summary.groundDriftDegPerDay)) {
        lines.push_back(unavailableReadings(summary));
        lines.push_back(revisionLine(summary));
        return lines;
    }
    lines.push_back(reading("Eclipse duration", summary.eclipseDurationS, "%.1f s (%.1f min)",
                            summary.eclipseDurationS / 60.0));
    lines.push_back(reading("Sunlit duration", summary.sunlitDurationS, "%.1f s (%.1f min)",
                            summary.sunlitDurationS / 60.0));
    lines.emplace_back("Durations are equivalent full eclipse/sun; penumbra is fractionally weighted.");
    // The sign carries the direction, the word spells it out: an author
    // who reads a drift of zero as "not moving" rather than "moving with
    // the ground" is one sign convention away from a wrong conclusion.
    std::string drift = reading("Ground drift", summary.groundDriftDegPerDay, "%+.1f deg/day");
    if (summary.groundDriftDegPerDay > 0.0) drift += " east";
    else if (summary.groundDriftDegPerDay < 0.0) drift += " west";
    lines.push_back(drift);
    if (summary.geostationary) lines.emplace_back(kGeostationaryBadge);
    lines.push_back(revisionLine(summary));
    return lines;
}

void drawOrbitSummary(const nsi::core::Installation& installation,
                      const nsi::systems::CellAtmosphereTransport* runtime) {
    const auto summary = orbitSummary(installation, runtime);
    if (!ImGui::TreeNodeEx("Orbit summary", ImGuiTreeNodeFlags_DefaultOpen)) return;
    for (const auto& text : orbitSummaryLines(summary)) {
        if (text == kGeostationaryBadge)
            ImGui::TextColored({0.45f, 0.90f, 0.60f, 1.0f}, "%s", text.c_str());
        else
            ImGui::TextUnformatted(text.c_str());
    }
    ImGui::TreePop();
}

} // namespace station::editor
