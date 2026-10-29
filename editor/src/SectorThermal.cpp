#include "SectorThermal.h"
#include <nsi/systems/Atmosphere.h>
#include <algorithm>

namespace station::editor {
SectorThermal sectorThermal(const nsi::core::Installation& installation,
    std::size_t level, std::size_t sector, const StationSystemsSnapshot* live) {
    SectorThermal result;
    result.skin = installation.exteriorSkin(level, sector);
    const auto atmosphere = nsi::systems::expandSector(installation, level, sector);
    if (!atmosphere) return result;
    result.hasAtmosphere = true;
    if (live) {
        const auto it = std::find_if(live->atmospheres.begin(), live->atmospheres.end(),
            [&](const auto& state) { return state.id == atmosphere->id; });
        if (it != live->atmospheres.end()) result.atmosphere = *it;
    }
    return result;
}
}
