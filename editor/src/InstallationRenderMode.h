#pragma once

#include <nsi/core/Installation.h>
#include <array>
#include <optional>
#include <vector>

namespace station { struct StationSystemsSnapshot; }
namespace nsi::systems { class CellAtmosphereTransport; }

namespace station::editor {
// Editor view state only: never serialized into an Installation or fed back
// into the simulation, and independent of the Cell/Wall/Sector editing mode.
enum class InstallationRenderMode { Default, Thermal, Pressure, Structure, Oxygen, Nitrogen, CarbonDioxide, WaterVapour };
enum class AtmosphereRenderQuantity { Mass, Percentage };
// Species order matches the native gas inventory: O2, N2, CO2, water vapour.
std::optional<std::size_t> atmosphereRenderComponent(InstallationRenderMode);
struct ComponentColourScale {
    float minimum{}, reference{}, maximum{};
    bool valid() const;
};
struct AtmosphereRenderSettings {
    AtmosphereRenderQuantity quantity=AtmosphereRenderQuantity::Percentage;
    std::array<ComponentColourScale,4> mass{{{0,3,6},{0,11,22},{0,0.01f,0.1f},{0,0.1f,0.3f}}};
    std::array<ComponentColourScale,4> percentage{{{0,21,100},{0,78,100},{0,0.04f,1},{0,1,10}}};
    ComponentColourScale& scale(std::size_t component) {
        return quantity==AtmosphereRenderQuantity::Mass ? mass.at(component) : percentage.at(component);
    }
    const ComponentColourScale& scale(std::size_t component) const {
        return quantity==AtmosphereRenderQuantity::Mass ? mass.at(component) : percentage.at(component);
    }
};

struct ThermalColourScale {
    float minimumC = -273.0f;
    float maximumC = 100.0f;
    float referenceC = 20.0f;
    bool valid() const;
};
struct PressureColourScale {
    float minimumKPa = 0.0f;
    float maximumKPa = 150.0f;
    float referenceKPa = 101.325f;
    bool valid() const;
};
struct HullRadiationColourScale {
    float minimumWPerM2=-100.0f;
    float maximumWPerM2=100.0f;
    float referenceWPerM2=0.0f;
    bool valid() const;
};
struct InstallationRenderSettings {
    InstallationRenderMode mode = InstallationRenderMode::Default;
    ThermalColourScale thermal;
    PressureColourScale pressure;
    AtmosphereRenderSettings atmosphere;
    bool hullBorder=true;
    HullRadiationColourScale hullRadiation;
    bool forceArrows=false;
    float forceHalfLengthN=10;
};
struct SectorColour {
    float red, green, blue;
    bool operator==(const SectorColour&) const = default;
};

// Smooth bounded force scale: zero length at zero load, half length at scale.
// No minimum arrow length or speed saturation; invalid inputs return zero.
double airflowLoadArrowFraction(double forceN,double halfLengthN=10);
SectorColour defaultSectorColour(nsi::core::Installation::SectorType);
// Neutral, outside the blue/green/red temperature scale: not a cold reading.
SectorColour unresolvedGasColour();
// Piecewise linear RGB interpolation with exact blue/green/red anchors.
// Values outside the scale saturate; invalid scales/temperatures have no colour.
std::optional<SectorColour> thermalColour(double temperatureC, const ThermalColourScale&);
std::optional<SectorColour> pressureColour(double pressureKPa, const PressureColourScale&);
// Net exterior radiation: positive inward heats, negative cools. Blue/green/red
// anchors at the chosen minimum/reference/maximum W/m2. Nonfinite values
// and invalid scales mean unavailable. Green is balance only at reference=0.
std::optional<SectorColour> netRadiationColour(double fluxWPerM2,const HullRadiationColourScale& = {});
// Area-weighted exterior surface flux per geometric wall, NaN if unavailable.
std::vector<double> wallNetRadiationFluxes(const nsi::core::Installation&,std::size_t level,
    const nsi::systems::CellAtmosphereTransport*);
std::optional<SectorColour> componentColour(double value, const ComponentColourScale&);
// Mass remains observable even in unresolved gas; percentage is wet mole %,
// includes water vapour, and is unavailable for unresolved/empty gas.
std::optional<double> atmosphereComponentValue(const std::array<double,4>& massKg,
    std::size_t component, AtmosphereRenderQuantity, bool resolvedGas);
// One colour per Sector on the requested Level, resolved from stable runtime
// atmosphere ids (not snapshot order or Sector names). Missing data uses Default.
std::vector<SectorColour> sectorRenderColours(const nsi::core::Installation&,
    std::size_t level, const InstallationRenderSettings&, const StationSystemsSnapshot* live = nullptr);
// One colour per geometric Cell. Native values are matched by Level, Cell
// index and stable Sector uid; absent data uses Default, unresolved gas is grey.
std::vector<SectorColour> cellRenderColours(const nsi::core::Installation&,
    std::size_t level,const InstallationRenderSettings&,const StationSystemsSnapshot* live=nullptr,
    const nsi::systems::CellAtmosphereTransport* structure=nullptr);
} // namespace station::editor
