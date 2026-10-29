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
    ImGui::TextUnformatted("Gas observations; see Structure for surfaces and real heat paths.");
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
        ImGui::TextWrapped("No whole-structure cooldown prediction. Gas capacity is separate from finite local solid capacity.");
        ImGui::EndTabItem();
    }
    if (drawConfiguration && ImGui::BeginTabItem("Configuration graph")) {
        drawConfiguration();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
}
