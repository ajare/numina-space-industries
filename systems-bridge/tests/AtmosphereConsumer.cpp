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
void thermalExpansion() {
    using namespace nsi::systems;
    auto scene=[](const Installation& i) {
        station::StationSystemsSettings settings;
        settings.enabled=true;
        settings.atmospheres=expandInstallation(i);
        auto thermal=expandThermal(i);
        settings.hull=thermal.hull;
        settings.thermalLinks=thermal.links;
        settings.solarCycle=thermal.solarCycle;
        require(settings.atmospherePathways.empty(),"No inferred gas pathways");
        std::string diagnostic;
        require(station::validateStationSystemsSettings(settings,diagnostic),diagnostic.c_str());
        return settings;
    };
    Installation single(1,1);
    auto settings=scene(single);
    require(settings.hull && settings.thermalLinks.size()==1,"One hull and drain");
    near(single.sideExteriorAreaM2(0,0),24.0);
    near(settings.hull->emittingAreaM2,24.0);
    near(settings.hull->projectedAreaM2,6.0);
    near(settings.hull->heatCapacityKJPerK,9.6);
    near(settings.solarCycle->constantLevel,0.0);
    for (auto type : {Installation::HullType::BareSkin, Installation::HullType::StandardInsulated,
                      Installation::HullType::MultiLayerBlanket}) {
        single.setHullType(type); const auto spec=Installation::hullSpec(type);
        settings=scene(single);
        near(settings.hull->emissivity,spec.emissivity);
        near(settings.hull->absorptivity,spec.absorptivity);
        near(settings.thermalLinks[0].conductanceKWPerK,spec.transmittanceWPerM2K*24/1000);
        require(settings.thermalLinks[0].aAtmosphereId==settings.atmospheres[0].id
            && settings.thermalLinks[0].bKind==station::ThermalNodeKind::Hull
            && settings.thermalLinks[0].bAtmosphereId=="hull","Typed Hull endpoint");
    }
    const auto twice=scene(Installation(2,2)); // Twice the single Cell perimeter.
    near(twice.hull->emittingAreaM2,48.0);
    near(twice.thermalLinks[0].conductanceKWPerK,2*settings.thermalLinks[0].conductanceKWPerK);
    Installation pair(2,1);
    pair.toggleWall(0,pair.level(0).verticalWall(1,0));
    const auto sharedHull=scene(pair);
    require(sharedHull.hull && sharedHull.thermalLinks.size()==2,"Two Sectors share exactly one Hull");
    near(sharedHull.hull->emittingAreaM2,36.0);
    for (const auto& link:sharedHull.thermalLinks) near(link.conductanceKWPerK,0.05*18/1000);
    auto partial=pair.serialize();
    partial.replace(partial.find("Earth-like"),10,"Void");
    Installation mixed; mixed.deserialize(partial);
    const auto mixedScene=scene(mixed);
    require(mixedScene.thermalLinks.size()==1,"Void-preset Sector must have no drain");
    near(mixedScene.hull->emittingAreaM2,18.0);
    Installation courtyard(3,3); courtyard.setCell(0,1,1,Installation::SectorType::Void);
    near(scene(courtyard).hull->emittingAreaM2,96.0); // Outer 12 + inner 4 edges.
    Installation lshape(2,2); lshape.setCell(0,1,1,Installation::SectorType::Void);
    near(scene(lshape).hull->emittingAreaM2,48.0);
    Installation buried(3,3);
    for (auto w : {buried.level(0).horizontalWall(1,1),buried.level(0).horizontalWall(1,2),
                   buried.level(0).verticalWall(1,1),buried.level(0).verticalWall(2,1)})
        buried.toggleWall(0,w);
    auto center=buried.level(0).cellData(1,1).sector;
    near(buried.sideExteriorAreaM2(0,center),0.0);
    auto enclosed=scene(buried);
    require(enclosed.atmospheres.size()==2 && enclosed.thermalLinks.size()==1,"Buried Sector has no drain");
    require(enclosed.thermalLinks[0].aAtmosphereId!=expandSector(buried,0,center)->id,"No zero-area link");
    Installation vacuum(std::vector<Installation::Level>{
        Installation::Level(1,1,Installation::SectorType::Void)});
    require(!scene(vacuum).hull && scene(vacuum).thermalLinks.empty(),"No hull without compartments");
    auto yaml=single.serialize(); auto p=yaml.find("Earth-like");
    yaml.replace(p,10,"Void"); Installation voidPreset; voidPreset.deserialize(yaml);
    require(!scene(voidPreset).hull && scene(voidPreset).thermalLinks.empty(),"Void preset has no drain or hull");
    // Existing geometry can explicitly remove every side Wall.
    Installation open(1,1);
    for (std::size_t w=0;w<open.level(0).walls().size();++w) open.toggleWall(0,w);
    require(!scene(open).hull && scene(open).thermalLinks.empty(),"No exterior skin means no hull");
    single.setHullType(std::nullopt);
    require(!scene(single).hull && scene(single).thermalLinks.empty(),"No hull opts out of links");
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
    try { check(); thermalExpansion(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
