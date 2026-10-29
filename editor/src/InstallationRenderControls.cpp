#include "InstallationRenderControls.h"
#include "imgui.h"

namespace station::editor {
void drawInstallationRenderModeCombo(InstallationRenderSettings& settings) {
    int mode = static_cast<int>(settings.mode);
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("Render mode",&mode,"Default\0Thermal\0Pressure\0"))
        settings.mode = static_cast<InstallationRenderMode>(mode);
    ImGui::SameLine();ImGui::Checkbox("Wind arrows",&settings.windArrows);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cell wind direction; arrow length saturates at 20 m/s. No forces on objects.");
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
            "Cells without live atmosphere data retain their Default appearance.");
    }
}
void drawPressureRenderControls(InstallationRenderSettings& settings) {
    if (settings.mode!=InstallationRenderMode::Pressure) return;
    if (ImGui::CollapsingHeader("Pressure rendering",ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& scale=settings.pressure;
        constexpr auto flags=ImGuiSliderFlags_AlwaysClamp;
        ImGui::SliderFloat("Minimum pressure (kPa)",&scale.minimumKPa,
            0.0f,scale.referenceKPa-0.001f,"%.3f",flags);
        ImGui::SliderFloat("Maximum pressure (kPa)",&scale.maximumKPa,
            scale.referenceKPa+0.001f,1000.0f,"%.3f",flags);
        ImGui::SliderFloat("Reference pressure (kPa)",&scale.referenceKPa,
            scale.minimumKPa+0.001f,scale.maximumKPa-0.001f,"%.3f",flags);
        ImGui::TextWrapped("Blue = minimum, green = reference, red = maximum. "
            "Pressures outside this range use the end colours. "
            "Cells without live atmosphere data retain their Default appearance.");
    }
}
} // namespace station::editor
