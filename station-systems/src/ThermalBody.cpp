#include <station/ThermalBody.h>
#include <station/Configuration.h>
#include "StationGasFlow.h"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace station {
namespace {
bool nonnegative(double x) { return std::isfinite(x) && x>=0; }
bool unit(double x) { return nonnegative(x) && x<=1; }
void validateFilm(const ThermalGasFilmSettings& f) {
    if (!nonnegative(f.continuumWPerM2K) || !unit(f.accommodation))
        throw std::invalid_argument("Invalid thermal gas-film settings");
}
}
ThermalBody::ThermalBody(const NativeThermalBodySettings& s) : capacity_(s.capacityJPerK) {
    if (!std::isfinite(capacity_) || capacity_<=0 || !nonnegative(s.initialTemperatureK))
        throw std::invalid_argument("Invalid thermal body capacity (J/K) or temperature (K)");
    energy_=capacity_*s.initialTemperatureK;
    if (!std::isfinite(energy_)) throw std::invalid_argument("Thermal body initial energy overflow");
}
ThermalBody ThermalBody::fromCarriedEnergy(double capacity,double energy) {
    ThermalBody body({capacity,0});
    if (!nonnegative(energy) || !std::isfinite(energy/capacity))
        throw std::invalid_argument("Invalid carried thermal body energy (J)");
    body.energy_=energy;return body;
}
void ThermalBody::addHeat(double joules) {
    const double next=energy_+joules;
    if (!std::isfinite(joules) || !nonnegative(next) || !std::isfinite(next/capacity_))
        throw std::invalid_argument("Invalid thermal body heat transfer (J)");
    energy_=next;
}
double thermalGasFilmWPerM2K(const ThermalGasFilmSettings& f,double pressure,double t,
    const std::array<double,4>& fractions) {
    validateFilm(f);
    if (!nonnegative(pressure) || !std::isfinite(t) || t<=0)
        throw std::invalid_argument("Invalid gas-film pressure (Pa) or mean temperature (K)");
    double sum=0;
    for (const double x:fractions) {
        if (!unit(x)) throw std::invalid_argument("Invalid gas-film wet mole fraction");
        sum+=x;
    }
    if (std::abs(sum-1)>1e-12) throw std::invalid_argument("Gas-film wet mole fractions must sum to one");
    constexpr std::array mass{StationMolarMassOxygenKgPerMol,StationMolarMassNitrogenKgPerMol,
        StationMolarMassCarbonDioxideKgPerMol,StationMolarMassWaterKgPerMol};
    constexpr double r=StationUniversalGasConstantJPerMolK;
    long double k=0;
    for (std::size_t i=0;i<mass.size();++i) {
        // cv_molar = cp_specific * M - R, from the same table as CellFluid.
        const double cv=1000*StationGasCpKJPerKgK[i]*mass[i]-r;
        k+=fractions[i]*(cv+r/2)/std::sqrt(2*std::numbers::pi_v<long double>*mass[i]*r*static_cast<long double>(t));
    }
    k*=f.accommodation/(2-f.accommodation);
    if (pressure==0 || k==0 || f.continuumWPerM2K==0) return 0;
    // Reciprocal form avoids overflow of K*p and its infinity/infinity limit.
    return static_cast<double>(f.continuumWPerM2K/(1+f.continuumWPerM2K/k/pressure));
}
void validateThermalContacts(std::size_t bodies,std::size_t gasCells,std::size_t cavities,
    std::span<const ThermalSolidContact> solid,std::span<const ThermalGasContact> gas,
    std::span<const ThermalRadiationContact> radiation) {
    for (const auto& c:solid)
        if (c.a>=bodies || c.b>=bodies || c.a==c.b || !nonnegative(c.conductanceWPerK))
            throw std::invalid_argument("Invalid solid thermal contact");
    for (const auto& c:gas) {
        validateFilm(c.film);
        if (c.body>=bodies || c.gasCell>=gasCells || !nonnegative(c.areaM2))
            throw std::invalid_argument("Invalid gas thermal contact");
    }
    for (const auto& c:radiation) {
        if (c.body>=bodies || !nonnegative(c.areaM2) || !unit(c.emissivity) || !unit(c.absorptivity))
            throw std::invalid_argument("Invalid radiation thermal contact");
        switch (c.boundary) {
        case ThermalRadiationBoundary::Cavity:
            if (c.cavity>=cavities) throw std::invalid_argument("Invalid thermal radiation cavity");
            break;
        case ThermalRadiationBoundary::Exterior: break;
        default: throw std::invalid_argument("Unknown thermal radiation boundary");
        }
    }
}
} // namespace station
