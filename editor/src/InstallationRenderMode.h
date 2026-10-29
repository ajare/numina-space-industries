#pragma once

#include <nsi/core/Installation.h>
#include <optional>
#include <vector>

namespace station { struct StationSystemsSnapshot; }

namespace station::editor {
// Editor view state only: never serialized into an Installation or fed back
// into the simulation, and independent of the Cell/Wall/Sector editing mode.
enum class InstallationRenderMode { Default, Thermal, Pressure };

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
struct InstallationRenderSettings {
    InstallationRenderMode mode = InstallationRenderMode::Default;
    ThermalColourScale thermal;
    PressureColourScale pressure;
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
// Piecewise linear RGB interpolation with exact blue/green/red anchors.
// Values outside the scale saturate; invalid scales/temperatures have no colour.
std::optional<SectorColour> thermalColour(double temperatureC, const ThermalColourScale&);
std::optional<SectorColour> pressureColour(double pressureKPa, const PressureColourScale&);
// One colour per Sector on the requested Level, resolved from stable runtime
// atmosphere ids (not snapshot order or Sector names). Missing data uses Default.
std::vector<SectorColour> sectorRenderColours(const nsi::core::Installation&,
    std::size_t level, const InstallationRenderSettings&, const StationSystemsSnapshot* live = nullptr);
// One colour per geometric Cell. Native values are matched by Level, Cell
// index and stable Sector uid; absent/unresolved temperatures use Default.
std::vector<SectorColour> cellRenderColours(const nsi::core::Installation&,
    std::size_t level,const InstallationRenderSettings&,const StationSystemsSnapshot* live=nullptr);
} // namespace station::editor
