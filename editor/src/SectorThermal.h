#pragma once

#include <nsi/core/Installation.h>
#include <station/StationSystems.h>
#include <optional>

namespace station::editor {
// Gas observations only. Local solid temperatures and heat paths are inspected
// through Structure; no shared-Hull C/G cooldown prediction is meaningful here.
struct SectorThermal {
    nsi::core::Installation::ExteriorSkin skin;
    bool hasAtmosphere = false;
    // Owned snapshot selected by stable atmosphere id, not Sector index.
    std::optional<StationAtmosphereSnapshot> atmosphere;
};
SectorThermal sectorThermal(const nsi::core::Installation&, std::size_t level,
    std::size_t sector, const StationSystemsSnapshot* live = nullptr);
}
