#include "SolarArrayControls.h"
#include "station/editor/EditorSession.h"
#include "imgui.h"
#include <cstdio>
#include <cmath>

namespace station::editor {
bool drawSolarArrayControls(EditorSession& session) {
    bool enabled = session.installation().solarArrayEnabled();
    ImGui::BeginDisabled(!session.editingEnabled());
    const bool changed = ImGui::Checkbox("Solar array enabled", &enabled);
    if (changed) session.setSolarArrayEnabled(enabled);
    ImGui::EndDisabled();
    return changed;
}
bool drawSolarArrayPanel(EditorSession& session, const std::optional<StationSystemsSnapshot>& snapshot) {
    auto array=session.installation().solarArray();
    if (!array) {
        ImGui::BeginDisabled(!session.editingEnabled());
        const bool added=ImGui::Button("Add solar array");
        if (added) session.setSolarArray(Installation::SolarArray{});
        ImGui::EndDisabled();
        return added;
    }
    // Missing Orbit is semantic absence. Never infer it from the irradiance
    // scalar, which is also zero in eclipse. Disabled takes precedence.
    // #69: Eclipse comes from the published systems-side shadow state and
    // never from zero output - a tracking failure, a fully worn blanket and
    // the switch all produce a zero that is not night.
    const auto* source=snapshot && !snapshot->power.sources.empty() ? &snapshot->power.sources.front() : nullptr;
    // Below half a tenth, "%.1f" prints a generating array as "0.0 kW",
    // which reads as a dead panel. Say it is under the readable floor
    // instead of rounding a measurement away.
    constexpr double kReadableFloorKW=0.05;
    char title[128];
    if (!session.installation().solarArrayEnabled() || (source && source->disabled))
        std::snprintf(title,sizeof title,"Solar array — Disabled");
    else if (!session.installation().orbit()) std::snprintf(title,sizeof title,"Solar array — No star");
    else if (!source) std::snprintf(title,sizeof title,"Solar array — Unavailable");
    else if (source->eclipse) std::snprintf(title,sizeof title,"Solar array — Eclipse");
    else if (source->availableKW<kReadableFloorKW)
        std::snprintf(title,sizeof title,"Solar array — <0.1 kW");
    else std::snprintf(title,sizeof title,"Solar array — %.1f kW",source->availableKW);
    // Stable ID: changing power/state must not reset the user's collapsed state.
    const std::string label=std::string(title)+"###solar-array";
    if (!ImGui::CollapsingHeader(label.c_str(),ImGuiTreeNodeFlags_DefaultOpen)) return false;
    bool changed=drawSolarArrayControls(session);
    ImGui::BeginDisabled(!session.editingEnabled());
    if (ImGui::InputDouble("Blanket area (m2)",&array->areaM2,1,10,"%.1f")) {
        if (std::isfinite(array->areaM2) && array->areaM2>=0) {
            session.setSolarArray(array); changed=true;
        } else array=session.installation().solarArray();
    }
    int technology=static_cast<int>(array->cellTechnology);
    if (ImGui::Combo("Cell technology",&technology,"Triple-junction GaAs\0Single-junction GaAs\0Space-grade silicon\0")) {
        array->cellTechnology=static_cast<Installation::SolarCellTechnology>(technology);
        session.setSolarArray(array); changed=true;
    }
    ImGui::TextUnformatted("Power is not connected to station consumers.");
    if (ImGui::Button("Remove solar array")) { session.setSolarArray(std::nullopt); changed=true; }
    ImGui::EndDisabled();
    return changed;
}
}
