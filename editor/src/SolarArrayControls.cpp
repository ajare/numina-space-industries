#include "SolarArrayControls.h"
#include "station/editor/EditorSession.h"
#include "imgui.h"

namespace station::editor {
bool drawSolarArrayControls(EditorSession& session) {
    bool enabled = session.installation().solarArrayEnabled();
    ImGui::BeginDisabled(!session.editingEnabled());
    const bool changed = ImGui::Checkbox("Solar array enabled", &enabled);
    if (changed) session.setSolarArrayEnabled(enabled);
    ImGui::EndDisabled();
    return changed;
}
}
