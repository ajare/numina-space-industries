#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace station {
// Identifies this opt-in contract, not the active playback backend or save schema.
inline constexpr std::string_view NativeThermalModelRevision = "local-structure-v2-cavity-candidate";
inline constexpr double ThermalBodyInitialTemperatureK = 293.15;
// Distinct from legacy machine-casing ThermalBodySettings (kJ/K, Celsius).
struct NativeThermalBodySettings {
    double capacityJPerK{}; // finite positive solid/object capacity; NEVER gas cv
    double initialTemperatureK{ThermalBodyInitialTemperatureK};
};
// Constant-capacity sensible energy with zero at 0 K. No gas EOS or geometry.
// Construction/Reset initializes inventory, not an applied heat source.
class ThermalBody {
public:
    explicit ThermalBody(const NativeThermalBodySettings&);
    static ThermalBody fromCarriedEnergy(double capacityJPerK,double energyJ);
    double capacityJPerK() const { return capacity_; }
    double energyJ() const { return energy_; }
    double temperatureK() const { return energy_/capacity_; }
    // Throws on invalid Kelvin/overflow without changing state. Pairwise energy
    // booking and whole-network rollback belong to the solver, not this value type.
    void addHeat(double joules);
private:
    double capacity_{},energy_{};
};
struct ThermalGasFilmSettings {
    double continuumWPerM2K{20}, accommodation{0.8};
};
// O2/N2/CO2/vapour WET mole fractions, sum 1. Uses shared runtime species cv,
// not a second EOS table. Pressure is absolute Pa, mean temperature positive K.
double thermalGasFilmWPerM2K(const ThermalGasFilmSettings&,double pressurePa,
    double meanTemperatureK,const std::array<double,4>& wetMoleFractions);
struct ThermalSolidContact {
    std::size_t a{},b{}; // body indices, distinct; zero conductance is inert
    double conductanceWPerK{};
};
struct ThermalGasContact {
    std::size_t body{},gasCell{};
    double areaM2{};
    ThermalGasFilmSettings film{};
};
enum class ThermalRadiationBoundary { Cavity, Exterior };
struct ThermalRadiationContact {
    std::size_t body{};
    ThermalRadiationBoundary boundary{ThermalRadiationBoundary::Cavity};
    std::size_t cavity{}; // only meaningful for Cavity; not an energy node
    double areaM2{},emissivity{},absorptivity{};
};
// Reset-time validation only; no topology ownership, insertion or solver implied.
// Multiple contacts per body are allowed (distinct physical surface areas).
void validateThermalContacts(std::size_t bodyCount,std::size_t gasCellCount,std::size_t cavityCount,
    std::span<const ThermalSolidContact>,std::span<const ThermalGasContact>,
    std::span<const ThermalRadiationContact>);
} // namespace station
