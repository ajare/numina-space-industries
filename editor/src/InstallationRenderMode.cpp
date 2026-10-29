#include "InstallationRenderMode.h"
#include <nsi/systems/Atmosphere.h>
#include <nsi/systems/CellAtmosphere.h>
#include <station/StationSystems.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace station::editor {
double airflowLoadArrowFraction(double forceN,double halfLengthN) {
    if (!std::isfinite(forceN) || !std::isfinite(halfLengthN) || forceN<=0 || halfLengthN<=0) return 0;
    return 1/(1+halfLengthN/forceN);
}
bool ThermalColourScale::valid() const {
    return std::isfinite(minimumC) && std::isfinite(referenceC) && std::isfinite(maximumC)
        && minimumC < referenceC && referenceC < maximumC;
}

bool PressureColourScale::valid() const {
    return std::isfinite(minimumKPa) && std::isfinite(referenceKPa) && std::isfinite(maximumKPa)
        && minimumKPa>=0 && minimumKPa<referenceKPa && referenceKPa<maximumKPa;
}

SectorColour unresolvedGasColour() {return {0.42f,0.42f,0.42f};}
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
    std::map<std::string,std::optional<double>> values;
    if (settings.mode != InstallationRenderMode::Default && settings.mode != InstallationRenderMode::Structure && live && live->enabled)
        for (const auto& air : live->atmospheres) {
            if (settings.mode==InstallationRenderMode::Thermal && air.spatial && !air.resolvedGas) values.emplace(air.id,std::nullopt);
            else values.emplace(air.id,settings.mode==InstallationRenderMode::Pressure ? air.pressureKPa : air.temperatureC);
        }
    const auto sectors = installation.level(level).sectors();
    std::vector<SectorColour> result;
    result.reserve(sectors.size());
    for (std::size_t index=0;index<sectors.size();++index) {
        auto colour = defaultSectorColour(sectors[index].type);
        if (!values.empty()) {
            if (const auto air = nsi::systems::expandSector(installation,level,index)) {
                const auto it = values.find(air->id);
                if (it != values.end()) {
                    const auto mapped=!it->second ? std::optional<SectorColour>{unresolvedGasColour()}
                        : settings.mode==InstallationRenderMode::Pressure
                            ? pressureColour(*it->second,settings.pressure) : thermalColour(*it->second,settings.thermal);
                    if (mapped) colour=*mapped;
                }
            }
        }
        result.push_back(colour);
    }
    return result;
}
std::vector<SectorColour> cellRenderColours(const nsi::core::Installation& installation,
    std::size_t level,const InstallationRenderSettings& settings,const StationSystemsSnapshot* live,
    const nsi::systems::CellAtmosphereTransport* structure) {
    const auto& grid=installation.level(level);std::vector<SectorColour> result;result.reserve(grid.cellCount());
    for (const auto& c:grid.cells()) result.push_back(defaultSectorColour(grid.sectors()[c.sector].type));
    if (settings.mode==InstallationRenderMode::Structure) {
        if (structure && structure->structure().enabled) {
            for (std::size_t i=0;i<grid.cellCount();++i) {
                const auto summary=structure->structuralSummary({level,
                    grid.offsetX()+static_cast<std::int64_t>(i%grid.width()),
                    grid.offsetY()+static_cast<std::int64_t>(i/grid.width())});
                const auto temperature=summary.all.areaWeightedTemperatureK();
                result[i]=temperature ? thermalColour(*temperature-273.15,settings.thermal).value_or(unresolvedGasColour())
                    : unresolvedGasColour();
            }
        }
        return result;
    }
    if (!live || !live->enabled || settings.mode==InstallationRenderMode::Default) return result;
    for (const auto& cell:live->cellAtmospheres) {
        if (cell.level!=level || cell.gridIndex>=grid.cellCount()) continue;
        const auto& sector=grid.sectors()[grid.cells()[cell.gridIndex].sector];
        if (sector.uid!=cell.sectorUid || sector.type!=nsi::core::Installation::SectorType::Interior
            || !nsi::systems::specFor(sector.atmosphere)) continue;
        const auto colour=settings.mode==InstallationRenderMode::Pressure ? pressureColour(cell.pressureKPa,settings.pressure)
            : cell.resolvedGas ? thermalColour(cell.temperatureC,settings.thermal) : std::optional<SectorColour>{unresolvedGasColour()};
        if (colour) result[cell.gridIndex]=*colour;
    }
    return result;
}
} // namespace station::editor
