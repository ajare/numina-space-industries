#pragma once
#include "station/Configuration.h"
#include <array>
#include <vector>
#include <cstddef>

namespace station {
// Calorically perfect ideal-gas engineering constants near room temperature.
// Species order is StationPathwayGases; CO remains a separate hazard ledger.
inline constexpr std::array<double,4> StationGasCpKJPerKgK{0.918,1.040,0.844,1.860};
struct GasMixture {
    double massKg{}, capacityKJPerK{}, gasConstantKJPerK{}, cpKJPerK{};
    double gamma() const { return cpKJPerK/capacityKJPerK; }
};
GasMixture gasMixture(const std::array<double,4>& mass);
struct GasVolume {
    std::array<double,4> mass{};
    double temperatureK{}, volumeM3{};
};
struct GasOpening {
    std::size_t a{SIZE_MAX},b{SIZE_MAX};
    double areaM2{}, dischargeCoefficient{0.7};
};
struct GasFlow {
    StationAtmosphereFlowRegime regime{StationAtmosphereFlowRegime::NoBulkFlow};
    double pressureRatio{1}, massKgPerS{}; // signed A -> B
};
double gasPressurePa(const GasVolume&);
GasFlow compressibleGasFlow(const GasVolume* a,const GasVolume* b,double area,double discharge);
struct GasTransfer {
    std::array<double,4> massKg{}; // includes counterdiffusion; signed A -> B
    double bulkMassKg{}, energyKJ{};
};
// Conservative shared substeps; donor fractions and pair pressure equilibrium
// bound every transfer. Closed openings cost no substeps. SIZE_MAX is vacuum.
std::vector<GasTransfer> integrateGasOpenings(std::vector<GasVolume>&,
    const std::vector<GasOpening>&,double seconds);
} // namespace station
