#include "SectorAtmospherePanel.h"
#include "SectorAtmosphere.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace station::editor {
namespace {
std::string value(double number,const char* unit) {
    if (!std::isfinite(number)) return std::string("-- ")+unit;
    char text[96]; std::snprintf(text,sizeof(text),"%.6g %s",number,unit); return text;
}
}
void drawSectorAtmosphere(const SectorThermal& state,std::span<const PressureSample> history,
    std::span<const AtmosphereSample> atmosphereHistory) {
    if (!state.hasAtmosphere) {
        ImGui::TextUnformatted("No atmosphere compartment; pressure and composition unavailable.");
        return;
    }
    if (!state.atmosphere) {
        ImGui::TextUnformatted("Live atmosphere stats unavailable: no matching simulation preview.");
        return;
    }
    const auto& a=*state.atmosphere;
    ImGui::TextUnformatted("Starting conditions");
    ImGui::Text("Starting pressure: %s",value(a.startingPressureKPa,"kPa").c_str());
    ImGui::Separator();
    ImGui::TextUnformatted("Live simulation values");
    ImGui::Text("Pressure: %s",value(a.pressureKPa,"kPa").c_str());
    if (history.empty()) ImGui::TextUnformatted("Pressure history unavailable.");
    else if (ImPlot::BeginPlot("Pressure — last minute###SectorPressure",ImVec2(-1,180),ImPlotFlags_NoLegend)) {
        const double now=history.back().seconds;
        ImPlot::SetupAxes("Simulation time (s)","Pressure (kPa)",ImPlotAxisFlags_None,ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1,std::max(0.0,now-60.0),std::max(60.0,now),ImGuiCond_Always);
        ImPlot::PlotLineG("Pressure",[](int index,void* data) {
            const auto& p=(*static_cast<std::span<const PressureSample>*>(data))[index];
            return ImPlotPoint(p.seconds,p.pressureKPa);
        },&history,static_cast<int>(history.size()));
        ImPlot::EndPlot();
    }
    ImGui::Text("Relative humidity: %s",value(100*a.relativeHumidity,"%").c_str());
    if (atmosphereHistory.empty()) ImGui::TextUnformatted("Relative humidity history unavailable.");
    else if (ImPlot::BeginPlot("Relative humidity — last minute###SectorHumidity",ImVec2(-1,180),ImPlotFlags_NoLegend)) {
        const double now=atmosphereHistory.back().seconds;
        ImPlot::SetupAxes("Simulation time (s)","Relative humidity (%)",ImPlotAxisFlags_None,ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1,std::max(0.0,now-60.0),std::max(60.0,now),ImGuiCond_Always);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1,0,100);
        ImPlot::PlotLineG("Relative humidity",[](int index,void* data) {
            const auto& s=(*static_cast<std::span<const AtmosphereSample>*>(data))[index];
            return ImPlotPoint(s.seconds,100*s.relativeHumidity);
        },&atmosphereHistory,static_cast<int>(atmosphereHistory.size()));
        ImPlot::EndPlot();
    }
    if (ImGui::BeginTable("Composition",4,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Gas"); ImGui::TableSetupColumn("Mass (kg)");
        ImGui::TableSetupColumn("Wet mole (%)"); ImGui::TableSetupColumn("Partial pressure (kPa)");
        ImGui::TableHeadersRow();
        for (const auto& gas:atmosphereComposition(a)) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(gas.name.data());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(value(gas.massKg,"").c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(gas.wetMolePercent ? value(*gas.wetMolePercent,"").c_str() : "--");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(value(gas.partialPressureKPa,"").c_str());
        }
        ImGui::EndTable();
    }
    if (atmosphereHistory.empty()) ImGui::TextUnformatted("Gas composition history unavailable.");
    else if (ImPlot::BeginPlot("Gas wet mole composition — last minute###SectorComposition",ImVec2(-1,220))) {
        const double now=atmosphereHistory.back().seconds;
        ImPlot::SetupAxes("Simulation time (s)","Wet mole (%)");
        ImPlot::SetupAxisLimits(ImAxis_X1,std::max(0.0,now-60.0),std::max(60.0,now),ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1,0,100,ImGuiCond_Always);
        const char* names[]={"Oxygen","Nitrogen","Carbon dioxide","Water vapour"};
        const ImVec4 colours[]={{0.2f,0.75f,0.9f,1},{0.3f,0.45f,0.8f,1},{0.95f,0.55f,0.2f,1},{0.65f,0.35f,0.85f,1}};
        struct Edge { std::span<const AtmosphereSample> samples; std::size_t boundary; };
        auto get=[](int index,void* data) {
            const auto& edge=*static_cast<Edge*>(data);
            const auto& s=edge.samples[index];
            return ImPlotPoint(s.seconds,(*s.wetMoleBoundsPercent)[edge.boundary]);
        };
        // Plot contiguous defined runs separately: never draw through vacuum
        // or shade invented zero-percent bands across missing composition.
        for (std::size_t gas=0;gas<4;++gas) {
            for (std::size_t start=0;start<atmosphereHistory.size();) {
                if (!atmosphereHistory[start].wetMoleBoundsPercent) { ++start; continue; }
                auto end=start+1;
                while (end<atmosphereHistory.size() && atmosphereHistory[end].wetMoleBoundsPercent) ++end;
                const auto run=atmosphereHistory.subspan(start,end-start);
                Edge lower{run,gas},upper{run,gas+1};
                ImPlot::SetNextFillStyle(colours[gas],0.85f);
                ImPlot::PlotShadedG(names[gas],get,&lower,get,&upper,static_cast<int>(run.size()));
                start=end;
            }
        }
        ImPlot::EndPlot();
    }
    ImGui::TextWrapped("Fractions include water vapour and are mole fractions, not mass fractions. "
        "At zero total pressure, fractions are undefined.");
    ImGui::Text("Carbon monoxide (separate hazard inventory): %s",value(a.carbonMonoxideKg,"kg").c_str());
    bool warning=false;
    auto warn=[&](bool active,const char* text) {
        if (active) { warning=true; ImGui::TextColored({1,0.65f,0.25f,1},"%s",text); }
    };
    warn(a.belowPressureLimit,"Low pressure"); warn(a.abovePressureLimit,"High pressure");
    warn(a.lowOxygen,"Low oxygen partial pressure"); warn(a.highCarbonDioxide,"High carbon dioxide partial pressure");
    if (!warning) ImGui::TextUnformatted("Within configured pressure and gas-quality limits.");
}
} // namespace station::editor
