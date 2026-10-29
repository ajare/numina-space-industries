#include "SectorAtmosphere.h"
#include <cmath>
#include <limits>

namespace station::editor {
std::array<AtmosphereGasReadout,4> atmosphereComposition(const StationAtmosphereSnapshot& a) {
    const std::array<double,4> moles{a.oxygenKg/StationMolarMassOxygenKgPerMol,a.nitrogenKg/StationMolarMassNitrogenKgPerMol,
        a.carbonDioxideKg/StationMolarMassCarbonDioxideKgPerMol,a.waterVapourKg/StationMolarMassWaterKgPerMol};
    double totalMoles=0;for (const auto m:moles) totalMoles+=m;
    std::size_t index=0;
    auto gas=[&](std::string_view name,double mass,double partial) {
        std::optional<double> fraction;
        if (a.spatial) {
            if (a.resolvedGas && a.pressureKPa>0 && std::isfinite(totalMoles) && totalMoles>0 && moles[index]>=0)
                fraction=100*moles[index]/totalMoles;
        } else if (std::isfinite(a.pressureKPa) && a.pressureKPa>0 && std::isfinite(partial) && partial>=0)
            fraction=100*partial/a.pressureKPa;
        ++index;
        return AtmosphereGasReadout{name,mass,partial,fraction};
    };
    return {gas("Oxygen",a.oxygenKg,a.oxygenPartialPressureKPa),
        gas("Nitrogen",a.nitrogenKg,a.nitrogenPartialPressureKPa),
        gas("Carbon dioxide",a.carbonDioxideKg,a.carbonDioxidePartialPressureKPa),
        gas("Water vapour",a.waterVapourKg,a.waterVapourPartialPressureKPa)};
}
AtmosphereSample atmosphereSample(double seconds,const StationAtmosphereSnapshot& a) {
    AtmosphereSample sample{seconds,a.spatial && !a.resolvedGas ? std::numeric_limits<double>::quiet_NaN() : a.relativeHumidity,std::nullopt};
    std::array<double,5> bounds{};
    const auto gases=atmosphereComposition(a);
    for (std::size_t gas=0;gas<gases.size();++gas) {
        if (!gases[gas].wetMolePercent || !std::isfinite(*gases[gas].wetMolePercent)) return sample;
        bounds[gas+1]=bounds[gas]+*gases[gas].wetMolePercent;
    }
    sample.wetMoleBoundsPercent=bounds;
    return sample;
}
} // namespace station::editor
