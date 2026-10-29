#include "SectorThermalPanel.h"
#include "SectorAtmospherePanel.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cmath>

namespace station::editor {
void drawSectorThermal(const SectorThermal& state, std::span<const TemperatureSample> history,
    const std::function<void()>& drawConfiguration, std::span<const PressureSample> pressureHistory,
    std::span<const AtmosphereSample> atmosphereHistory) {
    if (state.localStructure) ImGui::TextUnformatted("Gas observations; see Structure for surfaces and real heat paths.");
    else if (!state.hasAtmosphere) ImGui::TextUnformatted("No atmosphere compartment; no thermal links or Hull connection.");
    else if (state.isolated()) ImGui::TextUnformatted("Conductively isolated (no thermal links)");
    if (!ImGui::BeginTabBar("SectorSystems")) return;
    if (ImGui::BeginTabItem("Atmosphere")) {
        drawSectorAtmosphere(state,pressureHistory,atmosphereHistory);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Thermal")) {
        ImGui::TextUnformatted("Atmosphere thermal state");
        if (state.atmosphere) {
            const auto& air = *state.atmosphere;
            ImGui::TextUnformatted("Starting conditions");
            ImGui::Text("Starting temperature: %.3f C", air.startingTemperatureC);
            ImGui::Text("Starting thermal energy: %.3f kJ", air.startingEnergyKJ);
            ImGui::Text("Heat capacity: %.3f kJ/K (starting)", air.startingThermalCapacityKJPerK);
            if (air.inventoryThermodynamics)
                ImGui::TextUnformatted("Gas: inventory-dependent capacity; energy referenced to absolute zero.");
            ImGui::Separator();
            ImGui::TextUnformatted("Live simulation values");
            if (air.spatial && !air.resolvedGas) ImGui::TextUnformatted("Temperature: unavailable (vacuum/unresolved gas)");
            else ImGui::Text("Temperature: %.3f C%s",air.temperatureC,air.spatial ? " (capacity-weighted)" : "");
            ImGui::Text("Current heat capacity: %.6g kJ/K",air.thermalCapacityKJPerK);
            if (!history.empty()) {
                if (ImPlot::BeginPlot("Temperature — last minute###SectorTemperature", ImVec2(-1,180),
                        ImPlotFlags_NoLegend)) {
                    const double now = history.back().seconds;
                    ImPlot::SetupAxes("Simulation time (s)", "Temperature (C)",
                        ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
                    ImPlot::SetupAxisLimits(ImAxis_X1, std::max(0.0,now-60.0),
                        std::max(60.0,now), ImGuiCond_Always);
                    ImPlot::PlotLineG("Temperature", [](int index, void* data) {
                        const auto& sample = (*static_cast<std::span<const TemperatureSample>*>(data))[index];
                        return ImPlotPoint(sample.seconds,sample.temperatureC);
                    }, &history, static_cast<int>(history.size()));
                    ImPlot::EndPlot();
                }
            } else ImGui::TextUnformatted("Temperature history unavailable.");
            if (!air.spatial || air.resolvedGas) ImGui::Text("Temperature change: %+.3f C", air.temperatureC - air.startingTemperatureC);
            ImGui::Text("Thermal energy: %.3f kJ", air.currentEnergyKJ);
            if (air.spatial) {
                ImGui::Text("Wind kinetic energy: %.6g kJ",air.kineticEnergyKJ);
                ImGui::TextWrapped("Spatial advection includes pressure work and kinetic energy; the balance accounts for wind energy separately.");
            }
            ImGui::TextUnformatted("Last-step heat rates (kW; positive adds heat to this Sector)");
            ImGui::Text("Conduction: %+.6f | Reservoir exchange: %+.6f",
                air.lastConductedKW, air.lastExchangeIntoCompartmentKW);
            ImGui::Text("Occupants: %+.6f | Latent heat: %+.6f | Advection: %+.6f",
                air.lastOccupantHeatKW, air.lastLatentHeatKW, air.lastAdvectedKW);
            ImGui::Text("Crew: %+.6f | Crops: %+.6f | Algae: %+.6f",
                air.lastCrewHeatKW, air.lastCropHeatKW, air.lastAlgaeHeatKW);
            ImGui::Text("Fixtures: %+.6f | Hazard heat: %+.6f",
                air.lastFixtureHeatKW, air.lastHazardHeatKW);
            if (air.inventoryThermodynamics)
                ImGui::Text("Inventory-carried energy (non-Door processes): %+.6f kJ",air.inventoryEnergyKJ);
            ImGui::Text("Thermal energy balance residual: %+.6f kJ", air.residualKJ);
        } else if (state.hasAtmosphere) {
            ImGui::TextWrapped("Live atmosphere stats unavailable: no runtime snapshot for this compartment.");
        } else {
            ImGui::TextUnformatted("Atmosphere stats unavailable: no atmosphere compartment.");
        }
        ImGui::Separator();
        ImGui::Text("Exterior skin: %.2f m2 total", state.skin.totalM2());
        ImGui::Text("Side %.2f | Roof %.2f | Exposed floor underside %.2f m2",
            state.skin.sideM2, state.skin.roofM2, state.skin.floorUndersideM2);
        const bool noGas=state.atmosphere && (state.atmosphere->thermalCapacityKJPerK<=0
            || (state.atmosphere->spatial && !state.atmosphere->resolvedGas));
        if (!state.localStructure) {
        if (noGas) ImGui::TextUnformatted("Thermal timescales: unavailable (no resolved gas capacity)");
        else if (state.drainSeconds) ImGui::Text("Drain timescale: %.2f s", *state.drainSeconds);
        else ImGui::TextUnformatted("Drain timescale: infinite (no Hull link)");
        ImGui::TextWrapped("Timescales are local C/G responses with the other endpoint fixed. Ratio = diffusion / drain; not a prediction of nonlinear Hull cooling.");
        ImGui::TextWrapped("Flow is the last simulation step average, signed outward from this Sector. Before a step it is zero; unavailable without a runtime.");
        for (const auto& link : state.links) {
            ImGui::Separator();
            ImGui::TextUnformatted(link.neighbour.c_str());
            ImGui::Text("Conductance: %.6f W/K", link.conductanceKWPerK * 1000);
            if (link.outwardKW) {
                const double q = *link.outwardKW;
                ImGui::Text("Heat flow: %+.6f kW (%s)", q,
                    q > 0 ? "Sector -> neighbour" : q < 0 ? "neighbour -> Sector" : "no net flow");
            } else ImGui::TextUnformatted("Heat flow: unavailable");
            if (!noGas) ImGui::Text("%s timescale: %.2f s", link.hull ? "Drain" : "Diffusion", link.timescaleSeconds);
            if (!noGas && !link.hull) {
                if (link.diffusionToDrainRatio) ImGui::Text("Diffusion / drain: %.6f", *link.diffusionToDrainRatio);
                else ImGui::TextUnformatted("Diffusion / drain: 0 (infinite drain timescale)");
            }
        }
        } else ImGui::TextWrapped("No whole-structure cooldown prediction. Gas capacity is separate from finite local solid capacity.");
        ImGui::EndTabItem();
    }
    if (!state.localStructure && ImGui::BeginTabItem("Systems graph")) {
        ImGui::TextUnformatted("Conductive links only; no gas exchange");
        if (state.links.empty()) ImGui::TextUnformatted("No thermal connections");
        else {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float width = std::max(360.0f, ImGui::GetContentRegionAvail().x);
            const float height = 65.0f * state.links.size();
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec2 source(origin.x + 70, origin.y + height / 2);
            for (std::size_t i = 0; i < state.links.size(); ++i) {
                const auto& link = state.links[i];
                const ImVec2 target(origin.x + width - 85, origin.y + i * 65 + 24);
                draw->AddLine(source, target, IM_COL32(180,180,180,255), 2);
                // Arrow direction follows the signed last-step flow.
                if (link.outwardKW && *link.outwardKW != 0) {
                    const float sign = *link.outwardKW > 0 ? 1.0f : -1.0f;
                    float dx = target.x-source.x, dy = target.y-source.y;
                    const float length = std::sqrt(dx*dx+dy*dy);
                    dx = dx/length*sign; dy = dy/length*sign;
                    const ImVec2 tip((source.x+target.x)/2, (source.y+target.y)/2);
                    draw->AddTriangleFilled(tip, {tip.x-10*dx+5*dy,tip.y-10*dy-5*dx},
                        {tip.x-10*dx-5*dy,tip.y-10*dy+5*dx}, IM_COL32(255,190,70,255));
                }
                draw->AddRectFilled({target.x-65,target.y-16}, {target.x+65,target.y+16}, IM_COL32(60,75,90,255), 4);
                const auto label = link.hull ? std::string("Hull") : "Neighbour " + std::to_string(i + 1);
                draw->AddText({target.x-55,target.y-7}, IM_COL32_WHITE, label.c_str());
            }
            draw->AddRectFilled({source.x-65,source.y-16}, {source.x+65,source.y+16}, IM_COL32(55,95,65,255), 4);
            draw->AddText({source.x-57,source.y-7}, IM_COL32_WHITE, "Selected Sector");
            ImGui::Dummy({width,height});
            for (std::size_t i = 0; i < state.links.size(); ++i)
                ImGui::TextWrapped("%zu: %s [%s]", i + 1, state.links[i].neighbour.c_str(), state.links[i].id.c_str());
        }
        ImGui::EndTabItem();
    }
    if (drawConfiguration && ImGui::BeginTabItem("Configuration graph")) {
        drawConfiguration();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
}
