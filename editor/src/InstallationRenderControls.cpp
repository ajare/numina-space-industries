#include "InstallationRenderControls.h"
#include "imgui.h"

namespace station::editor {
void drawInstallationRenderModeCombo(InstallationRenderSettings& settings) {
    int mode = static_cast<int>(settings.mode);
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("Render mode",&mode,"Default\0Thermal\0Pressure\0"))
        settings.mode = static_cast<InstallationRenderMode>(mode);
    ImGui::SameLine();ImGui::Checkbox("Airflow load arrows",&settings.forceArrows);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Estimated force in N on a stationary reference object: area 1 m2, volume 1 m3, Cd 1. Drag + coarse pressure-gradient load; no rigid-body motion.");
    if (settings.forceArrows) {
        ImGui::SameLine();ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("Half-length load (N)",&settings.forceHalfLengthN,0.001f,100000.0f,"%.3g",ImGuiSliderFlags_Logarithmic|ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Arrow length = load / (load + scale). It shrinks smoothly to zero; half length at this force. Subpixel arrows are omitted.");
    }
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
            "Missing telemetry retains Default appearance; grey marks vacuum/unresolved gas, not low temperature.");
        const auto grey=unresolvedGasColour();
        ImGui::ColorButton("##UnresolvedGasColour",{grey.red,grey.green,grey.blue,1},ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_NoDragDrop,{12,12});
        ImGui::SameLine();ImGui::TextUnformatted("Grey = temperature unavailable (vacuum / unresolved gas)");
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
