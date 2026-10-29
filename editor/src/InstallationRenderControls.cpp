#include "InstallationRenderControls.h"
#include "imgui.h"

namespace station::editor {
void drawInstallationRenderModeCombo(InstallationRenderSettings& settings) {
    int mode = static_cast<int>(settings.mode);
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("Render mode",&mode,"Default\0Thermal\0"))
        settings.mode = static_cast<InstallationRenderMode>(mode);
}

void drawThermalRenderControls(InstallationRenderSettings& settings) {
    if (settings.mode != InstallationRenderMode::Thermal) return;
    if (ImGui::CollapsingHeader("Thermal rendering",ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& scale = settings.thermal;
        // Keep the green anchor strictly inside the scale, including Ctrl-click
        // text entry. Each slider has at least a 0.1 C gap from its neighbours.
        constexpr auto flags = ImGuiSliderFlags_AlwaysClamp;
        ImGui::SliderFloat("Minimum temperature (C)",&scale.minimumC,
            -273.0f,scale.referenceC-0.1f,"%.1f",flags);
        ImGui::SliderFloat("Maximum temperature (C)",&scale.maximumC,
            scale.referenceC+0.1f,1000.0f,"%.1f",flags);
        ImGui::SliderFloat("Reference temperature (C)",&scale.referenceC,
            scale.minimumC+0.1f,scale.maximumC-0.1f,"%.1f",flags);
        ImGui::TextWrapped("Blue = minimum, green = reference, red = maximum. "
            "Temperatures outside this range use the end colours. "
            "Sectors without live atmosphere data retain their Default appearance.");
    }
}
} // namespace station::editor
