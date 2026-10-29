#include "SectorThermal.h"
#include <nsi/systems/Atmosphere.h>
#include <algorithm>
#include <map>

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
    std::map<std::string, std::string> names;
    for (std::size_t l = 0; l < installation.levelCount(); ++l) {
        const auto sectors = installation.level(l).sectors();
        for (std::size_t s = 0; s < sectors.size(); ++s)
            if (auto a = nsi::systems::expandSector(installation, l, s))
                names[a->id] = "Level " + std::to_string(l) + " / " +
                    (sectors[s].name.empty() ? a->id : sectors[s].name) + " [" + a->id + "]";
    }
    for (const auto& link : nsi::systems::expandThermal(installation).links) {
        const bool atA = link.aKind == ThermalNodeKind::Atmosphere && link.aAtmosphereId == atmosphere->id;
        const bool atB = link.bKind == ThermalNodeKind::Atmosphere && link.bAtmosphereId == atmosphere->id;
        if (!atA && !atB) continue;
        SectorThermalLink row;
        row.id = link.id;
        row.hull = (atA ? link.bKind : link.aKind) == ThermalNodeKind::Hull;
        row.neighbour = row.hull ? "Hull" : names.at(atA ? link.bAtmosphereId : link.aAtmosphereId);
        row.conductanceKWPerK = link.conductanceKWPerK;
        row.timescaleSeconds = (result.atmosphere ? result.atmosphere->thermalCapacityKJPerK
            : atmosphere->thermalCapacityKJPerK) / link.conductanceKWPerK;
        if (row.hull) result.drainSeconds = row.timescaleSeconds;
        if (live) {
            const auto it = std::find_if(live->thermalLinks.begin(), live->thermalLinks.end(),
                [&](const auto& state) { return state.id == link.id; });
            if (it != live->thermalLinks.end()) row.outwardKW = (atA ? 1 : -1) * it->flowRateKW;
        }
        result.links.push_back(std::move(row));
    }
    if (result.drainSeconds)
        for (auto& row : result.links)
            if (!row.hull) row.diffusionToDrainRatio = row.timescaleSeconds / *result.drainSeconds;
    return result;
}
}
