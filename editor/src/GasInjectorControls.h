#pragma once
namespace station::editor {
struct GasInjectorSettings {
    int gas=0; // O2, N2, CO2, water vapour
    float kgPerS=0.1f;
};
// Call only in Injector mode; runtime controls remain enabled during playback.
void drawGasInjectorControls(GasInjectorSettings&);
}
