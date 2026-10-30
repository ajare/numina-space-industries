#include "InstallationRenderControls.h"
#include "imgui.h"
#include <cmath>
#include <limits>

namespace station::editor {
void drawInstallationRenderModeCombo(InstallationRenderSettings& settings) {
    int mode = static_cast<int>(settings.mode);
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("Render mode",&mode,"Default\0Thermal (gas)\0Pressure\0Structure (surfaces)\0Oxygen\0Nitrogen\0Carbon dioxide\0Water vapour\0"))
        settings.mode = static_cast<InstallationRenderMode>(mode);
    ImGui::SameLine();ImGui::Checkbox("Hull border",&settings.hullBorder);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show net hull radiation using the blue/green/red Hull rendering scale.");
    ImGui::SameLine();ImGui::Checkbox("Airflow load arrows",&settings.forceArrows);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Estimated force in N on a stationary reference object: area 1 m2, volume 1 m3, Cd 1. Drag + coarse pressure-gradient load; no rigid-body motion.");
    if (settings.forceArrows) {
        ImGui::SameLine();ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("Half-length load (N)",&settings.forceHalfLengthN,0.001f,100000.0f,"%.3g",ImGuiSliderFlags_Logarithmic|ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Arrow length = load / (load + scale). It shrinks smoothly to zero; half length at this force. Subpixel arrows are omitted.");
    }
}

void drawHullRenderControls(InstallationRenderSettings& settings) {
    if (!settings.hullBorder) return;
    if (ImGui::CollapsingHeader("Hull rendering",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("HullRendering");
        auto& scale=settings.hullRadiation;
        constexpr auto flags=ImGuiSliderFlags_AlwaysClamp;
        ImGui::SliderFloat("Minimum net radiation (W/m2)",&scale.minimumWPerM2,
            -10000.0f,scale.referenceWPerM2-0.1f,"%.1f",flags);
        ImGui::SliderFloat("Maximum net radiation (W/m2)",&scale.maximumWPerM2,
            scale.referenceWPerM2+0.1f,10000.0f,"%.1f",flags);
        ImGui::SliderFloat("Reference net radiation (W/m2)",&scale.referenceWPerM2,
            scale.minimumWPerM2+0.1f,scale.maximumWPerM2-0.1f,"%.1f",flags);
        ImGui::TextWrapped("Blue = minimum, green = reference, red = maximum. "
            "Negative flux cools the hull; positive flux heats it. Zero is radiative balance. "
            "Values outside the range use the end colours. Ctrl-click a slider to enter a precise value.");
        ImGui::PopID();
    }
}

void drawThermalRenderControls(InstallationRenderSettings& settings) {
    if (settings.mode != InstallationRenderMode::Thermal && settings.mode != InstallationRenderMode::Structure) return;
    if (settings.mode == InstallationRenderMode::Structure)
        ImGui::TextWrapped("Structure: area-weighted temperature of all exposed sides owned by each Cell (walls, floor and ceiling). Not air temperature. Grey = no measured surfaces. LocalStructure surfaces are independent of gas temperature.");
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
        ImGui::SameLine();ImGui::TextUnformatted(settings.mode == InstallationRenderMode::Structure ? "Grey = surface temperature unavailable" : "Grey = temperature unavailable (vacuum / unresolved gas)");
    }
}
void drawAtmosphereRenderControls(InstallationRenderSettings& settings) {
    const auto component=atmosphereRenderComponent(settings.mode);
    if (!component) return;
    constexpr std::array<const char*,4> headers{"Oxygen rendering","Nitrogen rendering","Carbon dioxide rendering","Water vapour rendering"};
    if (!ImGui::CollapsingHeader(headers[*component],ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("AtmosphereRendering");
    int quantity=static_cast<int>(settings.atmosphere.quantity);
    if (ImGui::Combo("Quantity",&quantity,"Mass (kg per Cell)\0Percentage (wet mole %)\0"))
        settings.atmosphere.quantity=static_cast<AtmosphereRenderQuantity>(quantity);
    const bool mass=settings.atmosphere.quantity==AtmosphereRenderQuantity::Mass;
    auto& scale=settings.atmosphere.scale(*component);
    // Adjacent representable values keep anchors strictly ordered even for
    // trace species or very large mass ranges, including direct text entry.
    const auto above=[](float v){return std::nextafter(v,std::numeric_limits<float>::infinity());};
    const auto below=[](float v){return std::nextafter(v,0.0f);};
    constexpr auto flags=ImGuiSliderFlags_AlwaysClamp;
    const float speed=mass ? 0.01f : 0.001f;
    ImGui::DragFloat(mass ? "Minimum mass (kg)" : "Minimum percentage (%)",&scale.minimum,
        speed,0,below(scale.reference),"%.6g",flags);
    ImGui::DragFloat(mass ? "Maximum mass (kg)" : "Maximum percentage (%)",&scale.maximum,
        speed,above(scale.reference),mass ? 1000000.0f : 100.0f,"%.6g",flags);
    const float referenceMinimum=above(scale.minimum),referenceMaximum=below(scale.maximum);
    ImGui::BeginDisabled(referenceMinimum>=referenceMaximum);
    ImGui::DragFloat(mass ? "Reference mass (kg)" : "Reference percentage (%)",&scale.reference,
        speed,referenceMinimum,referenceMaximum,"%.6g",flags);
    ImGui::EndDisabled();
    ImGui::TextWrapped(mass
        ? "Constituent mass in each Cell's full volume, including Sector height. Zero mass is blue; retained trace inventories remain measurable even when gas temperature is unresolved."
        : "Wet mole percentage of all gas in each Cell, including water vapour (not mass percentage or dry-gas fraction). Grey = empty/unresolved composition.");
    ImGui::TextWrapped("Blue = minimum, green = reference, red = maximum. Values outside the range use the end colours. Each gas and quantity retains its own scale; missing telemetry retains Default appearance.");
    ImGui::PopID();
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
