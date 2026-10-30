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
std::string number(double value, const char* format) {
    char buffer[96];
    std::snprintf(buffer, sizeof buffer, format, value);
    return buffer;
}

// One labelled reading. An unavailable value says so out loud rather than
// printing anything: a zero eclipse duration reads as a sunlit orbit and a
// zero drift reads as Geostationary, and neither is a fact about a sky the
// author has not authored.
std::string reading(std::string_view label, double value, const char* format) {
    std::string text(label);
    text += "  ";
    text += std::isfinite(value) ? number(value, format) : std::string("unavailable");
    return text;
}

std::string revisionLine(const OrbitSummary& summary) {
    std::string text = "Thermal model ";
    text += summary.modelRevision;
    text += " | preset ";
    text += summary.presetRevision;
    return text;
}

std::string unavailableReadings(const OrbitSummary& summary) {
    if (summary.overridden)
        return "Derived values unavailable: a hand-authored solar profile owns the "
               "stellar term, so the orbital sky is suppressed";
    return "Derived values unavailable: the authored numbers cannot make a sky";
}
} // namespace

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
    summary.betaAngleRad = orbit->betaAngleRad;
    summary.phaseOffsetS = orbit->phaseOffsetS;
    summary.albedo = orbit->albedo;
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
    lines.push_back(reading("Stellar irradiance", summary.stellarIrradianceWPerM2, "%.1f W/m^2"));
    lines.push_back(reading("Orbital period", summary.orbitalPeriodS, "%.1f s (%.1f min)"));
    lines.push_back(reading("Orbital radius", summary.radiusFromPlanetCentreM, "%.0f m (%.0f km)"));
    lines.push_back(reading("Beta angle", summary.betaAngleRad, "%.4f rad (%.1f deg)"));
    lines.push_back(reading("Phase offset", summary.phaseOffsetS, "%.1f s"));
    lines.push_back(reading("Albedo", summary.albedo, "%.3f"));
    if (!std::isfinite(summary.eclipseDurationS) || !std::isfinite(summary.sunlitDurationS)
        || !std::isfinite(summary.groundDriftDegPerDay)) {
        lines.push_back(unavailableReadings(summary));
        lines.push_back(revisionLine(summary));
        return lines;
    }
    lines.push_back(reading("Eclipse duration", summary.eclipseDurationS, "%.1f s (%.1f min)"));
    lines.push_back(reading("Sunlit duration", summary.sunlitDurationS, "%.1f s (%.1f min)"));
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
