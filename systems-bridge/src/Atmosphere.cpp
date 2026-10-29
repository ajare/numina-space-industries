#include <nsi/systems/Atmosphere.h>
#include <stdexcept>
#include <algorithm>
#include <map>

namespace nsi::systems {
namespace {
// Only three dry species are modelled. Fold argon and the other unmodelled
// gases into nitrogen rather than silently losing 0.93% of the dry pressure.
constexpr AtmospherePresetSpec EarthLike{
    0.2095, 1.0 - 0.2095 - 0.0004, 0.0004,
    101.325, 20.0, 0.40, 0.851,
    station::StationDefaultAdvectedSpecificHeatKJPerKgK,
    90.0, 110.0, 16.0, 5.0};
}

const AtmospherePresetSpec* specFor(core::Installation::AtmospherePreset preset) noexcept {
    return preset == core::Installation::AtmospherePreset::EarthLike ? &EarthLike : nullptr;
}

std::optional<station::AtmosphereSettings> expandSector(
    const core::Installation& installation, std::size_t level, std::size_t sector) {
    const auto sectors = installation.level(level).sectors();
    if (sector >= sectors.size()) throw std::out_of_range("Sector index");
    const auto& source = sectors[sector];
    const auto* spec = specFor(source.atmosphere);
    if (source.type == core::Installation::SectorType::Void || !spec) return std::nullopt;

    station::AtmosphereSettings result;
    result.id = "sector-" + std::to_string(level) + "-" + std::to_string(source.uid);
    if (result.id.size() > station::MaxStationStoreIdLength
        || result.id == station::StationExteriorAtmosphereId)
        throw std::logic_error("Invalid expanded atmosphere id");
    result.volumeM3 = source.volumeM3();
    const double vapourPressure = spec->relativeHumidity
        * station::stationSaturationVapourPressureKPa(spec->temperatureC);
    const double dryPressure = spec->totalPressureKPa - vapourPressure;
    const auto mass = [&](double pressureKPa, double molarMass) {
        return pressureKPa * 1000.0 * result.volumeM3 * molarMass
            / (station::StationUniversalGasConstantJPerMolK
                * (spec->temperatureC + station::StationCelsiusOffset));
    };
    result.startingOxygenKg = mass(spec->oxygenFraction * dryPressure,
        station::StationMolarMassOxygenKgPerMol);
    result.startingNitrogenKg = mass(spec->nitrogenFraction * dryPressure,
        station::StationMolarMassNitrogenKgPerMol);
    result.startingCarbonDioxideKg = mass(spec->carbonDioxideFraction * dryPressure,
        station::StationMolarMassCarbonDioxideKgPerMol);
    result.startingWaterVapourKg = mass(vapourPressure, station::StationMolarMassWaterKgPerMol);
    result.startingTemperatureC = spec->temperatureC;
    result.thermalCapacityKJPerK = spec->volumetricHeatCapacityKJPerKPerM3 * result.volumeM3;
    result.advectedSpecificHeatKJPerKgK = spec->advectedSpecificHeatKJPerKgK;
    result.minPressureKPa = spec->minPressureKPa;
    result.maxPressureKPa = spec->maxPressureKPa;
    result.minOxygenPartialPressureKPa = spec->minOxygenPartialPressureKPa;
    result.maxCarbonDioxidePartialPressureKPa = spec->maxCarbonDioxidePartialPressureKPa;
    const auto& assignment = source.atmosphereAssignment;
    result.occupants = assignment.occupants;
    result.oxygenUsePerOccupantKgPerS = assignment.oxygenUsePerOccupantKgPerS;
    result.co2ProductionPerOccupantKgPerS = assignment.co2ProductionPerOccupantKgPerS;
    result.sensibleHeatPerOccupantKW = assignment.sensibleHeatPerOccupantKW;
    result.moistureAdditionPerOccupantKgPerS = assignment.moistureAdditionPerOccupantKgPerS;
    result.leakRateKgPerS = assignment.leakRateKgPerS;
    result.reservoirId = assignment.reservoirId;
    result.exchangerConductanceKWPerK = assignment.exchangerConductanceKWPerK;
    if (assignment.fire) {
        const auto& fire = *assignment.fire;
        result.fire = station::AtmosphereFireSettings{fire.oxygenKgPerS,
            fire.carbonMonoxideKgPerS, fire.carbonDioxideKgPerS,
            fire.heatKW, fire.oxygenThresholdKg};
    }
    return result;
}

std::vector<DoorExpansion> expandDoors(const core::Installation& installation) {
    std::vector<DoorExpansion> result;
    for (std::size_t l=0;l<installation.levelCount();++l) {
        const auto& grid=installation.level(l);
        auto endpoint=[&](int x,int y) -> std::optional<std::string> {
            if (x<0 || y<0 || x>=int(grid.width()) || y>=int(grid.depth())
                || grid.cell(x,y)==core::Installation::SectorType::Void)
                return station::StationExteriorAtmosphereId;
            if (const auto air=expandSector(installation,l,grid.cellData(x,y).sector)) return air->id;
            return std::nullopt; // Void preset is absence, not exterior vacuum.
        };
        const auto horizontal=std::size_t(grid.width())*(std::size_t(grid.depth())+1);
        for (std::size_t w=0;w<grid.walls().size();++w) {
            if (grid.walls()[w].type!=core::Installation::WallType::Door) continue;
            const bool alongX=w<horizontal;
            const auto i=alongX ? w : w-horizontal;
            const auto stride=alongX ? std::size_t(grid.width()) : std::size_t(grid.width())+1;
            const int x=static_cast<int>(i%stride), y=static_cast<int>(i/stride);
            const auto a=endpoint(x-(alongX ? 0 : 1),y-(alongX ? 1 : 0));
            const auto b=endpoint(x,y);
            DoorExpansion door{l,w,std::nullopt};
            if (a && b && *a!=*b) {
                station::AtmospherePathwaySettings path;
                path.id="door-"+std::to_string(l)+"-"+std::to_string(w);
                path.aAtmosphereId=*a; path.bAtmosphereId=*b;
                // The clear opening is 0.9 m wide by 2 m high, not the full
                // Cell-edge Wall surface and independent of Sector height.
                path.areaM2=core::DoorOpeningAreaM2;
                path.openFraction=0.0;
                door.pathway=std::move(path);
            }
            result.push_back(std::move(door));
        }
    }
    return result;
}

std::vector<station::AtmosphereSettings> expandInstallation(const core::Installation& installation) {
    std::vector<station::AtmosphereSettings> result;
    for (std::size_t level = 0; level < installation.levelCount(); ++level)
        for (std::size_t sector = 0; sector < installation.level(level).sectors().size(); ++sector)
            if (auto atmosphere = expandSector(installation, level, sector))
                result.push_back(std::move(*atmosphere));
    return result;
}
ThermalExpansion expandThermal(const core::Installation& installation) {
    ThermalExpansion result;
    if (!installation.hullType()) return result;
    const auto spec = core::Installation::hullSpec(*installation.hullType());
    double totalArea = 0.0;
    for (std::size_t l = 0; l < installation.levelCount(); ++l) {
        for (std::size_t s = 0; s < installation.level(l).sectors().size(); ++s) {
            auto atmosphere = expandSector(installation, l, s);
            if (!atmosphere) continue;
            const double area = installation.exteriorSkin(l, s).totalM2();
            if (area <= 0.0) continue;
            totalArea += area;
            station::ThermalLinkSettings link;
            link.id = "drain-" + std::to_string(l) + "-"
                + std::to_string(installation.level(l).sectors()[s].uid);
            link.aAtmosphereId = atmosphere->id;
            link.bAtmosphereId = "hull";
            link.bKind = station::ThermalNodeKind::Hull;
            link.conductanceKWPerK = spec.transmittanceWPerM2K * area / 1000.0;
            result.links.push_back(std::move(link));
        }
    }
    if (installation.partitionType()) {
        const double u = core::Installation::partitionSpec(*installation.partitionType()).transmittanceWPerM2K;
        // Uids are Level-local. Both surface kinds accumulate into one canonical
        // pair of stable (Level, uid) endpoints, never transient Sector indices.
        using Endpoint = std::pair<std::size_t, std::int64_t>;
        std::map<std::pair<Endpoint, Endpoint>, double> sharedAreas;
        auto addArea = [&](Endpoint a, Endpoint b, double area) {
            sharedAreas[std::minmax(a, b)] += area;
        };
        for (std::size_t l = 0; l < installation.levelCount(); ++l) {
            const auto& grid = installation.level(l);
            auto shared = [&](std::uint32_t ax, std::uint32_t ay,
                              std::uint32_t bx, std::uint32_t by, std::size_t wall, double edge) {
                const auto type=grid.walls()[wall].type;
                if (type != core::Installation::WallType::Interior && type != core::Installation::WallType::Door) return;
                const auto& a = grid.sectorAt(ax, ay);
                const auto& b = grid.sectorAt(bx, by);
                if (a.uid == b.uid || a.type != core::Installation::SectorType::Interior
                    || b.type != core::Installation::SectorType::Interior
                    || !specFor(a.atmosphere) || !specFor(b.atmosphere)) return;
                addArea({l, a.uid}, {l, b.uid}, edge * core::LevelHeightM * std::min(a.height, b.height));
            };
            for (std::uint32_t y = 0; y < grid.depth(); ++y)
                for (std::uint32_t x = 1; x < grid.width(); ++x)
                    shared(x-1, y, x, y, grid.verticalWall(x, y), core::CellDepthM);
            for (std::uint32_t y = 1; y < grid.depth(); ++y)
                for (std::uint32_t x = 0; x < grid.width(); ++x)
                    shared(x, y-1, x, y, grid.horizontalWall(x, y), core::CellWidthM);
            // Count each floor interface once, from the lower claim's roof.
            // The upper Sector must start here, not merely claim this volume.
            for (std::uint32_t y = 0; y < grid.depth(); ++y) {
                for (std::uint32_t x = 0; x < grid.width(); ++x) {
                    const auto& a = grid.sectorAt(x, y);
                    if (a.type != core::Installation::SectorType::Interior || !specFor(a.atmosphere)) continue;
                    const auto top = l + std::size_t(a.height);
                    if (top >= installation.levelCount()) continue;
                    const auto& upper = installation.level(top);
                    const auto ux = std::int64_t(grid.offsetX()) + x - upper.offsetX();
                    const auto uy = std::int64_t(grid.offsetY()) + y - upper.offsetY();
                    if (ux < 0 || uy < 0 || ux >= upper.width() || uy >= upper.depth()) continue;
                    const auto& b = upper.sectorAt(static_cast<std::uint32_t>(ux), static_cast<std::uint32_t>(uy));
                    if (b.type != core::Installation::SectorType::Interior || !specFor(b.atmosphere)) continue;
                    addArea({l, a.uid}, {top, b.uid}, core::CellWidthM * core::CellDepthM);
                }
            }
        }
        for (const auto& [pair, area] : sharedAreas) {
            const auto endpointId = [](Endpoint e) {
                return std::to_string(e.first) + "-" + std::to_string(e.second);
            };
            station::ThermalLinkSettings link;
            // Keep existing same-Level identities; cross-Level pairs include
            // both namespaces so equal uids on different Levels cannot collide.
            link.id = "pair-" + endpointId(pair.first) + "-"
                + (pair.first.first == pair.second.first ? std::to_string(pair.second.second)
                                                        : endpointId(pair.second));
            link.aAtmosphereId = "sector-" + endpointId(pair.first);
            link.bAtmosphereId = "sector-" + endpointId(pair.second);
            link.conductanceKWPerK = u * area / 1000.0;
            result.links.push_back(std::move(link));
        }
    }
    if (totalArea > 0.0) {
        station::HullSettings hull;
        hull.emittingAreaM2 = totalArea;
        hull.projectedAreaM2 = totalArea / 4.0;
        hull.heatCapacityKJPerK = 0.4 * totalArea;
        hull.emissivity = spec.emissivity;
        hull.absorptivity = spec.absorptivity;
        result.hull = hull;
        result.solarCycle = station::ProfileSettings{0.0};
    }
    return result;
}
} // namespace nsi::systems
