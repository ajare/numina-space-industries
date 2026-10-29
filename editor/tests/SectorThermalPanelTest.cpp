#include "SectorThermalPanel.h"
#include "SimulationPlayback.h"
#include "SectorAtmosphere.h"
#include "SectorAtmospherePanel.h"
#include "imgui_internal.h"
#include <cmath>
#include <limits>
#include <string>
#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"
#include <iostream>
#include <stdexcept>

int main() {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;
    io.DisplaySize={1200,1800};
    io.DeltaTime=1.0f/60.0f;
    unsigned char* pixels; int width,height;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    int result=0;
    try {
        using namespace station::editor;
        nsi::core::Installation installation(1,1);
        SimulationPlayback playback;
        playback.reset(installation);
        const auto id=playback.runtime()->snapshot().atmospheres[0].id;
        auto initial=playback.runtime()->snapshot().atmospheres[0];
        double fractions=0;
        for (const auto& gas:atmosphereComposition(initial)) {
            if (!gas.wetMolePercent) throw std::runtime_error("Positive-pressure mole fraction absent");
            fractions+=*gas.wetMolePercent;
            if (std::abs(*gas.wetMolePercent-100*gas.partialPressureKPa/initial.pressureKPa)>1e-9)
                throw std::runtime_error("Composition used mass instead of wet mole fraction");
        }
        if (std::abs(fractions-100)>1e-8) throw std::runtime_error("Wet fractions do not sum to 100");
        const auto sampled=atmosphereSample(0,initial);
        if (!sampled.wetMoleBoundsPercent || std::abs(sampled.wetMoleBoundsPercent->back()-100)>1e-8)
            throw std::runtime_error("Composition stack does not total 100 percent");
        const auto gases=atmosphereComposition(initial);
        for (std::size_t gas=0;gas<gases.size();++gas)
            if (std::abs((*sampled.wetMoleBoundsPercent)[gas+1]-(*sampled.wetMoleBoundsPercent)[gas]-*gases[gas].wetMolePercent)>1e-8)
                throw std::runtime_error("Stack band is not the gas wet mole percentage");
        initial.pressureKPa=0;
        if (atmosphereSample(0,initial).wetMoleBoundsPercent)
            throw std::runtime_error("Vacuum fabricated composition stack");
        for (const auto& gas:atmosphereComposition(initial))
            if (gas.wetMolePercent) throw std::runtime_error("Vacuum fabricated composition percentages");
        initial.pressureKPa=std::numeric_limits<double>::quiet_NaN();
        if (atmosphereComposition(initial)[0].wetMolePercent) throw std::runtime_error("Nonfinite pressure fabricated fraction");
        // Exercise empty, initial-single-point and rolling-window plots through
        // real ImGui/ImPlot rendering without requiring an OpenGL display.
        for (int stage=0;stage<4;++stage) {
            if (stage==2) {
                playback.setSpeedIndex(4); playback.togglePlaying();
                for (int frame=0;frame<70;++frame) playback.update(0.1);
            }
            if (stage==3) playback.reset(installation);
            const auto snapshot=playback.runtime()->snapshot();
            const auto state=sectorThermal(installation,0,0,&snapshot);
            for (int frame=0;frame<2;++frame) {
                ImGui::NewFrame();
                ImGui::SetNextWindowSize({1000,1500});
                ImGui::Begin("Sector");
                drawSectorThermal(state,stage==0 ? std::span<const TemperatureSample>{}
                                               : playback.temperatureHistory(id),{},
                    stage==0 ? std::span<const PressureSample>{} : playback.pressureHistory(id),
                    stage==0 ? std::span<const AtmosphereSample>{} : playback.atmosphereHistory(id));
                ImGui::End();
                ImGui::Render();
                if (!ImGui::GetDrawData()->Valid || ImGui::GetDrawData()->TotalVtxCount==0)
                    throw std::runtime_error("Thermal panel did not render");
            }
        }
        const auto current=playback.snapshotFor(installation);
        auto state=sectorThermal(installation,0,0,&*current);
        auto capture=[&](const SectorThermal& state) {
            ImGui::NewFrame(); ImGui::SetNextWindowSize({1000,1500}); ImGui::Begin("Atmosphere readouts");
            ImGui::LogToBuffer(); drawSectorAtmosphere(state,playback.pressureHistory(id),playback.atmosphereHistory(id));
            if (state.atmosphere) {
                const auto* pressure=ImPlot::GetPlot("Pressure — last minute###SectorPressure");
                const auto* humidity=ImPlot::GetPlot("Relative humidity — last minute###SectorHumidity");
                const auto* composition=ImPlot::GetPlot("Gas wet mole composition — last minute###SectorComposition");
                if (!pressure || !humidity || !composition || pressure->FrameRect.Max.y>=humidity->FrameRect.Min.y
                    || humidity->FrameRect.Max.y>=composition->FrameRect.Min.y)
                    throw std::runtime_error("Atmosphere plots missing or in wrong vertical order");
                if (composition->Items.GetItemCount()!=4)
                    throw std::runtime_error("Composition plot is missing a species band");
            }
            const std::string text=ImGui::GetCurrentContext()->LogBuffer.c_str(); ImGui::LogFinish();
            ImGui::End(); ImGui::Render(); return text;
        };
        auto text=capture(state);
        auto ordered=[](const std::string& text,const char* starting,const char* live) {
            const auto first=text.find(starting), second=text.find(live);
            if (first==std::string::npos || second==std::string::npos || first>=second)
                throw std::runtime_error("Starting conditions must precede live simulation values");
        };
        ordered(text,"Starting conditions","Starting pressure:");
        ordered(text,"Starting pressure:","Live simulation values");
        ordered(text,"Live simulation values","Pressure:");
        std::string thermalText;
        for (int frame=0;frame<2;++frame) {
            ImGui::NewFrame(); ImGui::SetNextWindowSize({850,700}); ImGui::Begin("Sector");
            auto* tabs=ImGui::GetCurrentContext()->TabBars.GetByKey(ImGui::GetID("SectorSystems"));
            if (!tabs) throw std::runtime_error("Sector tabs unavailable");
            tabs->NextSelectedTabId=ImHashStr("Thermal",0,tabs->ID);
            ImGui::LogToBuffer(); drawSectorThermal(state,playback.temperatureHistory(id));
            thermalText=ImGui::GetCurrentContext()->LogBuffer.c_str(); ImGui::LogFinish();
            ImGui::End(); ImGui::Render();
        }
        ordered(thermalText,"Starting conditions","Starting temperature:");
        ordered(thermalText,"Starting temperature:","Live simulation values");
        ordered(thermalText,"Starting thermal energy:","Live simulation values");
        ordered(thermalText,"Heat capacity:","Live simulation values");
        ordered(thermalText,"Live simulation values","Temperature:");
        ordered(thermalText,"Live simulation values","Thermal energy:");
        for (const char* label:{"Pressure:","Relative humidity:","Oxygen","Nitrogen","Carbon dioxide","Water vapour","Wet mole","Partial pressure"})
            if (text.find(label)==std::string::npos) throw std::runtime_error("Atmosphere tab omitted a readout");
        state.atmosphere->belowPressureLimit=true; state.atmosphere->lowOxygen=true;
        state.atmosphere->highCarbonDioxide=true;
        text=capture(state);
        for (const char* warning:{"Low pressure","Low oxygen","High carbon dioxide"})
            if (text.find(warning)==std::string::npos) throw std::runtime_error("Atmosphere tab omitted warning");
        state.atmosphere.reset();
        if (capture(state).find("unavailable")==std::string::npos) throw std::runtime_error("Missing telemetry not labelled");
        state.hasAtmosphere=false;
        if (capture(state).find("No atmosphere compartment")==std::string::npos) throw std::runtime_error("Missing compartment not labelled");
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; result=1; }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return result;
}
