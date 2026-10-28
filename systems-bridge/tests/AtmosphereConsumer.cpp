#include <nsi/systems/Atmosphere.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace {
using Installation = nsi::core::Installation;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected) {
    require(std::abs(actual - expected) <= 1e-12 * std::max(1.0, std::abs(expected)),
        "floating-point mismatch");
}
std::array<double, 4> masses(const station::AtmosphereSettings& a) {
    return {a.startingOxygenKg, a.startingNitrogenKg,
        a.startingCarbonDioxideKg, a.startingWaterVapourKg};
}
std::array<double, 4> pressures(const station::AtmosphereSettings& a) {
    const std::array molarMasses{station::StationMolarMassOxygenKgPerMol,
        station::StationMolarMassNitrogenKgPerMol,
        station::StationMolarMassCarbonDioxideKgPerMol,
        station::StationMolarMassWaterKgPerMol};
    auto result = masses(a);
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = station::stationAtmospherePartialPressureKPa(
            result[i], molarMasses[i], a.volumeM3, a.startingTemperatureC);
    return result;
}
void check() {
    const auto* spec = nsi::systems::specFor(Installation::AtmospherePreset::EarthLike);
    require(spec != nullptr, "missing Earth-like spec");
    require(!nsi::systems::specFor(Installation::AtmospherePreset::Void), "Void has a spec");
    near(spec->oxygenFraction + spec->nitrogenFraction + spec->carbonDioxideFraction, 1.0);
    near(spec->totalPressureKPa, 101.325);
    near(spec->temperatureC, 20.0);
    near(spec->relativeHumidity, 0.40);
    near(spec->volumetricHeatCapacityKJPerKPerM3, 0.851);

    const Installation single(1, 1);
    const auto a = nsi::systems::expandSector(single, 0, 0);
    require(a.has_value(), "Interior omitted");
    near(a->volumeM3, 12.0);
    near(a->thermalCapacityKJPerK, 0.851 * 12.0);
    near(a->advectedSpecificHeatKJPerKgK, 1.005);
    near(a->minPressureKPa, 90.0);
    near(a->maxPressureKPa, 110.0);
    near(a->minOxygenPartialPressureKPa, 16.0);
    near(a->maxCarbonDioxidePartialPressureKPa, 5.0);
    const auto p = pressures(*a);
    near(p[0] + p[1] + p[2] + p[3], spec->totalPressureKPa);
    const auto saturation = station::stationSaturationVapourPressureKPa(spec->temperatureC);
    near(p[3], spec->relativeHumidity * saturation);
    require(p[3] <= saturation, "vapour exceeds saturation");
    near(p[0], spec->oxygenFraction * (spec->totalPressureKPa - p[3]));
    near(p[1], spec->nitrogenFraction * (spec->totalPressureKPa - p[3]));
    near(p[2], spec->carbonDioxideFraction * (spec->totalPressureKPa - p[3]));

    const auto b = *nsi::systems::expandSector(Installation(2, 1), 0, 0);
    near(b.volumeM3, 2.0 * a->volumeM3);
    near(b.thermalCapacityKJPerK, 2.0 * a->thermalCapacityKJPerK);
    for (std::size_t i = 0; i < 4; ++i) {
        near(masses(b)[i], 2.0 * masses(*a)[i]);
        near(pressures(b)[i], p[i]);
    }
    Installation tall(std::vector<Installation::Level>{
        Installation::Level(3, 1), Installation::Level(3, 1, Installation::SectorType::Void)});
    tall.setSector(0, 0, "Tall", 2);
    near(nsi::systems::expandSector(tall, 0, 0)->volumeM3, 72.0);
    require(!nsi::systems::expandSector(tall, 1, 0), "Void geometry emitted air");
    near(tall.level(1).sectors()[0].volumeM3(), 0.0);
    const Installation vacuum(std::vector<Installation::Level>{
        Installation::Level(2, 2, Installation::SectorType::Void)});
    require(nsi::systems::expandInstallation(vacuum).empty(), "all-Void emitted air");

    // A floored but unpressurised Sector is distinct from Void geometry.
    auto yaml = single.serialize();
    auto pos = yaml.find("Earth-like");
    require(pos != std::string::npos, "missing serialized preset");
    yaml.replace(pos, std::string("Earth-like").size(), "Void");
    Installation unpressurised;
    unpressurised.deserialize(yaml);
    require(!nsi::systems::expandSector(unpressurised, 0, 0), "Void preset emitted air");

    Installation multi(std::vector<Installation::Level>{Installation::Level(2, 1),
        Installation::Level(2, 1)});
    multi.toggleWall(0, multi.level(0).verticalWall(1, 0));
    multi.toggleWall(1, multi.level(1).verticalWall(1, 0));
    auto expanded = nsi::systems::expandInstallation(multi);
    require(expanded.size() == 4, "missing compartments");
    // The return type itself excludes connections. Guard this contract as well
    // as the composed settings, so adding links cannot silently change it.
    static_assert(std::is_same_v<decltype(expanded), std::vector<station::AtmosphereSettings>>);
    station::StationSystemsSettings settings;
    settings.atmospheres = expanded;
    require(settings.thermalLinks.empty(), "bridge emitted thermal links");
    require(settings.atmospherePathways.empty(), "bridge emitted pathways");
    std::set<std::string> ids;
    std::string diagnostic;
    for (const auto& compartment : settings.atmospheres) {
        require(ids.insert(compartment.id).second, "duplicate id");
        require(compartment.id.size() <= station::MaxStationStoreIdLength, "id too long");
        require(compartment.id != station::StationExteriorAtmosphereId, "reserved id");
        require(station::validateStationWaterGasWiring(settings, compartment, diagnostic, false, nullptr),
            diagnostic.c_str());
    }
    require(station::validateStationSystemsSettings(settings, diagnostic), diagnostic.c_str());
    settings.atmospheres = nsi::systems::expandInstallation(vacuum);
    require(station::validateStationSystemsSettings(settings, diagnostic), diagnostic.c_str());
    require(a->id == "sector-0-" + std::to_string(single.level(0).sectors()[0].uid), "id format");
    bool threw = false;
    try { (void)nsi::systems::expandSector(single, 0, 1); }
    catch (const std::out_of_range&) { threw = true; }
    require(threw, "invalid Sector index accepted");
}
}
int main() {
    try { check(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
