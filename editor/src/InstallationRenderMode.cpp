#include "InstallationRenderMode.h"
#include <nsi/systems/Atmosphere.h>
#include <station/StationSystems.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace station::editor {
bool ThermalColourScale::valid() const {
    return std::isfinite(minimumC) && std::isfinite(referenceC) && std::isfinite(maximumC)
        && minimumC < referenceC && referenceC < maximumC;
}

SectorColour defaultSectorColour(nsi::core::Installation::SectorType type) {
    return type == nsi::core::Installation::SectorType::Interior
        ? SectorColour{126/255.0f,168/255.0f,200/255.0f}
        : SectorColour{26/255.0f,29/255.0f,34/255.0f};
}

std::optional<SectorColour> thermalColour(double temperatureC, const ThermalColourScale& scale) {
    if (!std::isfinite(temperatureC) || !scale.valid()) return std::nullopt;
    if (temperatureC <= scale.referenceC) {
        const float t = static_cast<float>(std::clamp(
            (temperatureC-scale.minimumC) / (double(scale.referenceC)-scale.minimumC),0.0,1.0));
        return SectorColour{0,t,1-t};
    }
    const float t = static_cast<float>(std::clamp(
        (temperatureC-scale.referenceC) / (double(scale.maximumC)-scale.referenceC),0.0,1.0));
    return SectorColour{t,1-t,0};
}

std::vector<SectorColour> sectorRenderColours(const nsi::core::Installation& installation,
    std::size_t level, const InstallationRenderSettings& settings, const StationSystemsSnapshot* live) {
    std::map<std::string,double> temperatures;
    if (settings.mode == InstallationRenderMode::Thermal && live)
        for (const auto& air : live->atmospheres) temperatures.emplace(air.id,air.temperatureC);
    const auto sectors = installation.level(level).sectors();
    std::vector<SectorColour> result;
    result.reserve(sectors.size());
    for (std::size_t index=0;index<sectors.size();++index) {
        auto colour = defaultSectorColour(sectors[index].type);
        if (!temperatures.empty()) {
            if (const auto air = nsi::systems::expandSector(installation,level,index)) {
                const auto it = temperatures.find(air->id);
                if (it != temperatures.end())
                    if (const auto thermal = thermalColour(it->second,settings.thermal)) colour = *thermal;
            }
        }
        result.push_back(colour);
    }
    return result;
}
} // namespace station::editor
