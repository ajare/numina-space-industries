#include "GasInjectorControls.h"
#include "imgui.h"
namespace station::editor {
void drawGasInjectorControls(GasInjectorSettings& settings) {
    if (!ImGui::CollapsingHeader("Gas injector",ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("GasInjector");
    ImGui::Combo("Gas",&settings.gas,"Oxygen\0Nitrogen\0Carbon dioxide\0Water vapour\0");
    ImGui::SliderFloat("Rate (kg/s)",&settings.kgPerS,0.000001f,10.0f,"%.6g",
        ImGuiSliderFlags_Logarithmic|ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped("Hold left mouse on a Cell to inject 20 C gas at rest. Drag to move the injector; leaving the grid or releasing stops injection. Only Interior Cells with modelled atmospheres accept gas.");
    ImGui::TextWrapped("Playing: rate follows simulation time and speed. Paused: rate follows wall time; transport, condensation and thermal evolution wait for Play. Reset removes injected gas. Injection never edits the saved Installation.");
    ImGui::PopID();
}
}
