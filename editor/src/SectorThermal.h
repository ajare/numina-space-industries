#pragma once

#include <nsi/core/Installation.h>
#include <station/StationSystems.h>
#include <optional>
#include <string>
#include <vector>

namespace station::editor {
struct SectorThermalLink {
    std::string id, neighbour;
    bool hull = false;
    double conductanceKWPerK = 0;
    // Signed outward from the selected Sector; last runtime step average.
    std::optional<double> outwardKW;
    double timescaleSeconds = 0;
    std::optional<double> diffusionToDrainRatio;
};
struct SectorThermal {
    nsi::core::Installation::ExteriorSkin skin;
    bool hasAtmosphere = false;
    // Owned snapshot for the selected stable atmosphere id; absent before a
    // runtime exists or when that compartment is not present in the snapshot.
    std::optional<StationAtmosphereSnapshot> atmosphere;
    std::optional<double> drainSeconds;
    std::vector<SectorThermalLink> links;
    bool isolated() const { return hasAtmosphere && links.empty(); }
};
// Local C/G response times (other endpoint held fixed), not two-body
// equilibration times or a prediction of the radiating Hull's nonlinear cooling.
SectorThermal sectorThermal(const nsi::core::Installation&, std::size_t level,
    std::size_t sector, const StationSystemsSnapshot* live = nullptr);
}
