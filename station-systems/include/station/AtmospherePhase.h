#pragma once

namespace station {
// Existing instantaneous vapour/latent-heat equilibrium. Capacity is the
// pre-condensation gas capacity in kJ/K, heat rate kW, temperatures Celsius.
// Condensate carries its sensible energy at the resulting temperature.
struct VapourThermalCoupling {
    double temperatureC{};
    double condensedKg{};
};
VapourThermalCoupling solveVapourThermalCoupling(double capacityKJPerK,
    double startTemperatureC,double externalHeatKW,double vapourKg,
    double volumeM3,double seconds);
} // namespace station
