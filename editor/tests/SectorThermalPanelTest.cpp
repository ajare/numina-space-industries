#include "SectorThermalPanel.h"
#include "SimulationPlayback.h"
#include "imgui.h"
#include "implot.h"
#include <iostream>
#include <stdexcept>

int main() {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;
    io.DisplaySize={1024,768};
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
                ImGui::SetNextWindowSize({850,700});
                ImGui::Begin("Sector");
                drawSectorThermal(state,stage==0 ? std::span<const TemperatureSample>{}
                                               : playback.temperatureHistory(id));
                ImGui::End();
                ImGui::Render();
                if (!ImGui::GetDrawData()->Valid || ImGui::GetDrawData()->TotalVtxCount==0)
                    throw std::runtime_error("Thermal panel did not render");
            }
        }
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; result=1; }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return result;
}
