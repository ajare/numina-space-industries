#include <nsi/systems/Atmosphere.h>
#include <stdexcept>

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

std::vector<station::AtmosphereSettings> expandInstallation(const core::Installation& installation) {
    std::vector<station::AtmosphereSettings> result;
    for (std::size_t level = 0; level < installation.levelCount(); ++level)
        for (std::size_t sector = 0; sector < installation.level(level).sectors().size(); ++sector)
            if (auto atmosphere = expandSector(installation, level, sector))
                result.push_back(std::move(*atmosphere));
    return result;
}
} // namespace nsi::systems
