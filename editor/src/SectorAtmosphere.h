#pragma once
#include <station/AtmosphereMetrics.h>
#include <array>
#include <optional>
#include <string_view>

namespace station::editor {
struct AtmosphereGasReadout {
    std::string_view name;
    double massKg;
    double partialPressureKPa;
    // Ideal-gas wet mole fraction, not mass fraction. Undefined at zero pressure.
    std::optional<double> wetMolePercent;
};
std::array<AtmosphereGasReadout,4> atmosphereComposition(const StationAtmosphereSnapshot&);
struct AtmosphereSample {
    double seconds;
    double relativeHumidity; // fraction; displayed as percent
    // Cumulative stack boundaries: 0, O2, O2+N2, +CO2, +water vapour (%).
    // Absent when composition is undefined (e.g. zero total pressure).
    std::optional<std::array<double,5>> wetMoleBoundsPercent;
};
AtmosphereSample atmosphereSample(double seconds,const StationAtmosphereSnapshot&);
} // namespace station::editor
