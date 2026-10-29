#pragma once

#include <nsi/core/Installation.h>
#include <station/Configuration.h>
#include <optional>
#include <vector>

namespace nsi::systems {

struct AtmospherePresetSpec {
    double oxygenFraction; // fractions of dry pressure, summing to one
    double nitrogenFraction;
    double carbonDioxideFraction;
    double totalPressureKPa;
    double temperatureC;
    double relativeHumidity;
    double volumetricHeatCapacityKJPerKPerM3; // density times cv, not cp
    double advectedSpecificHeatKJPerKgK;
    double minPressureKPa;
    double maxPressureKPa;
    double minOxygenPartialPressureKPa;
    double maxCarbonDioxidePartialPressureKPa;
};

// Void has no intensive spec and materialises as no compartment.
const AtmospherePresetSpec* specFor(core::Installation::AtmospherePreset) noexcept;
// Invalid indices throw std::out_of_range. Void geometry or preset returns nullopt.
std::optional<station::AtmosphereSettings> expandSector(
    const core::Installation&, std::size_t level, std::size_t sector);
// Atmosphere-only output. Compose with expandThermal for geometry-authored heat loss.
std::vector<station::AtmosphereSettings> expandInstallation(const core::Installation&);

struct ThermalExpansion {
    std::optional<station::HullSettings> hull;
    std::vector<station::ThermalLinkSettings> links;
    std::optional<station::ProfileSettings> solarCycle;
};
// Total exterior skin (side, roof, exposed floor underside) and shared-Wall
// partition conduction. One pair link per Level-local sorted Sector uid pair.
// No hull means no links. No gas pathways are ever inferred.
ThermalExpansion expandThermal(const core::Installation&);

} // namespace nsi::systems
