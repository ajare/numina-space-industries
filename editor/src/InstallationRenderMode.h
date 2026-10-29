#pragma once

#include <nsi/core/Installation.h>
#include <optional>
#include <vector>

namespace station { struct StationSystemsSnapshot; }

namespace station::editor {
// Editor view state only: never serialized into an Installation or fed back
// into the simulation, and independent of the Cell/Wall/Sector editing mode.
enum class InstallationRenderMode { Default, Thermal };

struct ThermalColourScale {
    float minimumC = -273.0f;
    float maximumC = 100.0f;
    float referenceC = 20.0f;
    bool valid() const;
};
struct InstallationRenderSettings {
    InstallationRenderMode mode = InstallationRenderMode::Default;
    ThermalColourScale thermal;
};
struct SectorColour {
    float red, green, blue;
    bool operator==(const SectorColour&) const = default;
};

SectorColour defaultSectorColour(nsi::core::Installation::SectorType);
// Piecewise linear RGB interpolation with exact blue/green/red anchors.
// Values outside the scale saturate; invalid scales/temperatures have no colour.
std::optional<SectorColour> thermalColour(double temperatureC, const ThermalColourScale&);
// One colour per Sector on the requested Level, resolved from stable runtime
// atmosphere ids (not snapshot order or Sector names). Missing data uses Default.
std::vector<SectorColour> sectorRenderColours(const nsi::core::Installation&,
    std::size_t level, const InstallationRenderSettings&, const StationSystemsSnapshot* live = nullptr);
} // namespace station::editor
