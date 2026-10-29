#pragma once
#include <nsi/systems/StructuralTopology.h>
#include <span>

namespace nsi::systems {
enum class StructuralOrientation { Side, Floor, Ceiling };
struct StructuralSurfaceObservation {
    StructuralSurfaceId id;
    std::optional<StructuralOwner> owner; // current binary Door pose, not authored ownership
    StructuralOrientation orientation{};
    double areaM2{}, temperatureK{}, capacityJPerK{};
    long double storedEnergyJ{}; // Kelvin reference, one side's allocated capacity
    std::optional<std::size_t> cavity, gasCell;
    std::optional<double> gasTemperatureK; // missing/unresolved gas remains unavailable
    double gasCapacityJPerK{}, gasContactWPerK{}; // gas-only EOS capacity; current film coefficient
};
struct StructuralSurfaceSummary {
    std::size_t surfaces{};
    double areaM2{}, capacityJPerK{}, minTemperatureK{}, maxTemperatureK{};
    long double storedEnergyJ{}, areaTemperatureKm2{};
    // Explicit area-weighted surface temperature, NOT gas or equilibrated room temperature.
    std::optional<double> areaWeightedTemperatureK() const {
        if(areaM2==0) return std::nullopt;
        return static_cast<double>(areaTemperatureKm2/areaM2);
    }
};
struct StructuralSelectionSummary {
    StructuralSurfaceSummary all, floor, ceiling, sides;
};
struct StructuralPortalObservation {
    std::size_t index{}, a{};
    std::optional<std::size_t> b; // missing = exterior, never missing gas
    double areaM2{};
    std::optional<long double> irradianceA, irradianceB, outwardPowerW;
};
struct StructuralQueryResult { std::size_t written{}, total{}; }; // total allows caller pagination/truncation UI
struct StructuralContactObservation {
    StructuralSurfaceId a,b;
    double conductanceWPerK{};
};
} // namespace nsi::systems
