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

bool PressureColourScale::valid() const {
    return std::isfinite(minimumKPa) && std::isfinite(referenceKPa) && std::isfinite(maximumKPa)
        && minimumKPa>=0 && minimumKPa<referenceKPa && referenceKPa<maximumKPa;
}

SectorColour defaultSectorColour(nsi::core::Installation::SectorType type) {
    return type == nsi::core::Installation::SectorType::Interior
        ? SectorColour{126/255.0f,168/255.0f,200/255.0f}
        : SectorColour{26/255.0f,29/255.0f,34/255.0f};
}

namespace {
SectorColour scalarColour(double value,double minimum,double reference,double maximum) {
    if (value<=reference) {
        const float t=static_cast<float>(std::clamp((value-minimum)/(reference-minimum),0.0,1.0));
        return {0,t,1-t};
    }
    const float t=static_cast<float>(std::clamp((value-reference)/(maximum-reference),0.0,1.0));
    return {t,1-t,0};
}
}
std::optional<SectorColour> thermalColour(double temperatureC, const ThermalColourScale& scale) {
    if (!std::isfinite(temperatureC) || !scale.valid()) return std::nullopt;
    return scalarColour(temperatureC,scale.minimumC,scale.referenceC,scale.maximumC);
}

std::optional<SectorColour> pressureColour(double pressureKPa, const PressureColourScale& scale) {
    if (!std::isfinite(pressureKPa) || !scale.valid() || pressureKPa<0) return std::nullopt;
    return scalarColour(pressureKPa,scale.minimumKPa,scale.referenceKPa,scale.maximumKPa);
}

std::vector<SectorColour> sectorRenderColours(const nsi::core::Installation& installation,
    std::size_t level, const InstallationRenderSettings& settings, const StationSystemsSnapshot* live) {
    std::map<std::string,double> values;
    if (settings.mode != InstallationRenderMode::Default && live && live->enabled)
        for (const auto& air : live->atmospheres)
            values.emplace(air.id,settings.mode==InstallationRenderMode::Pressure ? air.pressureKPa : air.temperatureC);
    const auto sectors = installation.level(level).sectors();
    std::vector<SectorColour> result;
    result.reserve(sectors.size());
    for (std::size_t index=0;index<sectors.size();++index) {
        auto colour = defaultSectorColour(sectors[index].type);
        if (!values.empty()) {
            if (const auto air = nsi::systems::expandSector(installation,level,index)) {
                const auto it = values.find(air->id);
                if (it != values.end()) {
                    const auto mapped=settings.mode==InstallationRenderMode::Pressure
                        ? pressureColour(it->second,settings.pressure) : thermalColour(it->second,settings.thermal);
                    if (mapped) colour=*mapped;
                }
            }
        }
        result.push_back(colour);
    }
    return result;
}
} // namespace station::editor
