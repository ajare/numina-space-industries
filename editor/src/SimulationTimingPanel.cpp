#include "SimulationTimingPanel.h"
#include "imgui.h"
#include "implot.h"
#include <array>
#include <cstdio>

namespace station::editor {
void drawSimulationTiming(const SimulationStepTiming& timing) {
    if (!ImGui::CollapsingHeader("Update time",ImGuiTreeNodeFlags_DefaultOpen)) return;
    const auto average=timing.average();
    ImGui::Text("Solvers: %.1f us | Lua: %.1f us",average.solverMicroseconds,average.luaMicroseconds);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Last 10 successful updates. Solvers: native gas, sources, phase and thermal processing. Lua: lifecycle preparation/recovery. Excludes diagnostics, snapshot publication, history and GUI work; failed attempts are not sampled.");
    ImGui::TextDisabled("Last %zu / 50 updates (5 s at 10 Hz)",timing.sampleCount());
    if (timing.sampleCount()==0) {ImGui::TextDisabled("No completed updates yet.");return;}
    // Item-major layout required by PlotBarGroups: all solver bars, then Lua.
    std::array<double,2*SimulationStepTiming::windowSize> values{};
    const auto count=timing.sampleCount();
    double maximum=0.001;
    for (std::size_t i=0;i<count;++i) {
        const auto& sample=timing.sample(i);
        values[i]=sample.solverMicroseconds/1000;
        values[count+i]=sample.luaMicroseconds/1000;
        maximum=std::max(maximum,sample.totalMicroseconds()/1000);
    }
    if (ImPlot::BeginPlot("###SimulationUpdateTiming",ImVec2(-1,190),ImPlotFlags_NoInputs)) {
        ImPlot::SetupAxes("Recent updates (s at 10 Hz)","Time (ms)",ImPlotAxisFlags_NoHighlight,ImPlotAxisFlags_NoHighlight);
        ImPlot::SetupAxisLimits(ImAxis_X1,-49.5,0.5,ImPlotCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_X1,[](double value,char* text,int size,void*) {
            return std::snprintf(text,size,"%.1f",value/10);
        });
        ImPlot::SetupAxisLimits(ImAxis_Y1,0,maximum*1.15,ImPlotCond_Always);
        const char* labels[]={"Fluid / thermal solvers","Lua"};
        ImPlot::PlotBarGroups(labels,values.data(),2,static_cast<int>(count),0.9,1-static_cast<double>(count),ImPlotBarGroupsFlags_Stacked);
        ImPlot::EndPlot();
    }
}
} // namespace station::editor
