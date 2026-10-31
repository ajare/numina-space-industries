#include "station/Configuration.h"
#include "station/Persistence.h"
#include "station/StationConnections.h"
#include "ConfigurationScenarios.h"
#include "SolarArrayAuthoringScenarios.h"

#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, char const* message)
{
    if (!condition) throw std::runtime_error(message);
}
void checkConfiguration()
{
    auto settings = station_test::pathwaySettings();
    std::string diagnostic;
    require(station::validateStationSystemsSettings(settings, diagnostic), diagnostic.c_str());
    auto const original = settings;
    auto refuse = [&](station::StationSystemsSettings const& bad, char const* field) {
        require(!station::validateStationSystemsSettings(bad, diagnostic), "invalid pathway passed pure validation");
        require(diagnostic.find("opening") != std::string::npos && diagnostic.find(field) != std::string::npos,
            "pathway refusal did not name the entry and offending field/reference");
    };
    station_test::invalidPathways(settings, refuse);
    { auto bad = settings; bad.atmospherePathways[0].id.clear();
        require(!station::validateStationSystemsSettings(bad, diagnostic), "empty pathway id accepted"); }
    { auto bad = settings; bad.atmospherePathways[0].aAtmosphereId = station::StationExteriorAtmosphereId;
        bad.atmospherePathways[0].bAtmosphereId = station::StationExteriorAtmosphereId; refuse(bad, "self-loop"); }
    { auto bad = settings; bad.atmospheres[0].id = station::StationExteriorAtmosphereId;
        require(!station::validateStationSystemsSettings(bad, diagnostic)
            && diagnostic.find("reserved") != std::string::npos, "exterior id was allowed as a real compartment"); }
    { auto bad = settings; bad.atmospherePathways[0].bAtmosphereId.clear(); refuse(bad, "unknown"); }
    // The per-type count ceilings on atmospheres, pathways and thermal links
    // were removed: a configuration far past the old 8-compartment and
    // 64-pathway caps must validate rather than be refused on count alone.
    auto many = settings;
    many.atmospherePathways.clear();
    for (size_t i = 0; i < 256; ++i)
        many.atmospherePathways.push_back({"p" + std::to_string(i), "a", "b", 1, 0});
    require(station::validateStationSystemsSettings(many, diagnostic), diagnostic.c_str());
    if (!many.atmospheres.empty()) {
        auto const seed = many.atmospheres.front();
        while (many.atmospheres.size() < 64) {
            auto compartment = seed;
            compartment.id = "comp" + std::to_string(many.atmospheres.size());
            many.atmospheres.push_back(compartment);
        }
        require(station::validateStationSystemsSettings(many, diagnostic), diagnostic.c_str());
    }
    require(settings == original, "validation changed the valid configuration");

    // Exercise the catalogue and connection-identity dependencies of validation,
    // not just the no-Monitor fast path.
    station::StationMonitorSettings monitor;
    monitor.id = "oxygen"; monitor.subjectType = "atmosphere"; monitor.subjectId = "a";
    monitor.metric = "oxygenPartialPressureKPa";
    settings.monitors.push_back(monitor);
    require(station::validateStationSystemsSettings(settings, diagnostic), diagnostic.c_str());
    settings.monitors[0].metric = "not-a-metric";
    require(!station::validateStationSystemsSettings(settings, diagnostic)
        && diagnostic.find("oxygen") != std::string::npos, "unknown Monitor metric accepted");
    auto const graph = station::station_connections::build(settings);
    require(!graph.links.empty(), "representative configuration has no connections");
    settings.monitors[0].subjectType = "connection";
    settings.monitors[0].subjectId = station::stationConnectionMonitorSubjectId(graph.links.front().designKey);
    settings.monitors[0].metric = "lossFraction";
    require(station::validateStationSystemsSettings(settings, diagnostic), diagnostic.c_str());
    settings.monitors[0].subjectId = "unknown-connection";
    require(!station::validateStationSystemsSettings(settings, diagnostic)
        && diagnostic.find("oxygen") != std::string::npos, "unknown Monitor connection accepted");
}
}
int main()
{
    try {
        checkConfiguration();
        station_test::solar::catalogue();
        station_test::solar::realWorldCalibration();
        station_test::solar::unknownTechnologyRefused();
        station_test::solar::settingsCarryTheFields();
        station_test::solar::blanketAreaBounds();
        auto settings = station_test::pathwaySettings();
        auto text = station::serializeStationConfiguration(settings);
        station::StationSystemsSettings restored;
        std::string diagnostic = "stale";
        require(station::parseStationConfiguration(text, restored, &diagnostic), diagnostic.c_str());
        require(diagnostic.empty() && restored == settings, "standalone round trip changed settings");
        auto const original = restored;
        auto invalidText = text;
        auto const area = invalidText.find("areaM2: ");
        require(area != std::string::npos, "missing authored pathway area");
        auto const value = area + std::string("areaM2: ").size();
        invalidText.replace(value, invalidText.find('\n', value) - value, "-1");
        require(!station::parseStationConfiguration(invalidText, restored, &diagnostic),
            "physically invalid text accepted");
        require(!diagnostic.empty() && restored == original, "invalid text mutated settings");
        for (auto const& malformed : {std::string("["), std::string("schemaVersion: 138"),
                std::string("schemaVersion: 139\nstationSystems: {}"),
                std::string("schemaVersion: 64\nstationSystems: {}")}) {
            require(!station::parseStationConfiguration(malformed, restored, &diagnostic), "malformed input accepted");
            require(!diagnostic.empty() && restored == original, "refusal mutated settings or lost diagnostic");
        }
        // A newer feature cannot be smuggled through an older schema context.
        auto position = text.find("schemaVersion: ");
        auto end = text.find('\n', position);
        text.replace(position, end - position, "schemaVersion: 65");
        require(!station::parseStationConfiguration(text, restored, &diagnostic), "old schema accepted pathways");
        require(restored == original, "compatibility refusal mutated destination");
        auto invalid = settings;
        invalid.atmospherePathways.front().areaM2 = -1;
        bool refused = false;
        try { (void)station::serializeStationConfiguration(invalid); }
        catch (std::exception const&) { refused = true; }
        require(refused, "invalid configuration serialized");
        station::StationSystemsSettings empty;
        auto legacy = station::serializeStationConfiguration(empty);
        auto version = legacy.find("111");
        require(version != std::string::npos, "feature-absent schema changed");
        legacy.replace(version, 3, "96");
        auto step = legacy.find("  systemsStepWorldTicks:");
        require(step != std::string::npos, "missing current step field");
        legacy.erase(step, legacy.find('\n', step) - step + 1);
        require(station::parseStationConfiguration(legacy, restored, &diagnostic), diagnostic.c_str());
        empty.systemsStepWorldTicks = 1; // Pre-104 documents use the historical one-tick step.
        require(restored == empty, "legacy configuration changed");
    }
    catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
