#include "SectorAtmosphere.h"
#include <cmath>

namespace station::editor {
std::array<AtmosphereGasReadout,4> atmosphereComposition(const StationAtmosphereSnapshot& a) {
    auto gas=[&](std::string_view name,double mass,double partial) {
        std::optional<double> fraction;
        if (std::isfinite(a.pressureKPa) && a.pressureKPa>0 && std::isfinite(partial) && partial>=0)
            fraction=100*partial/a.pressureKPa;
        return AtmosphereGasReadout{name,mass,partial,fraction};
    };
    return {gas("Oxygen",a.oxygenKg,a.oxygenPartialPressureKPa),
        gas("Nitrogen",a.nitrogenKg,a.nitrogenPartialPressureKPa),
        gas("Carbon dioxide",a.carbonDioxideKg,a.carbonDioxidePartialPressureKPa),
        gas("Water vapour",a.waterVapourKg,a.waterVapourPartialPressureKPa)};
}
AtmosphereSample atmosphereSample(double seconds,const StationAtmosphereSnapshot& a) {
    AtmosphereSample sample{seconds,a.relativeHumidity,std::nullopt};
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
