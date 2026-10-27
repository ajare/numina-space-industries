#include "station/StationConnections.h"
#include "station/AtmosphereMetrics.h"
#include <algorithm>
namespace station
{
using namespace station_connections;
namespace {
template<class Entries>
auto findId(Entries const& entries, std::string_view id)
{ return std::find_if(entries.begin(), entries.end(), [&](auto const& item) { return item.id == id; }); }
}
std::string stationConnectionMonitorSubjectId(StationConnectionKey const& key)
{
	// Length-prefixed full key: delimiter-bearing authored IDs cannot alias.
	std::string result;
	for (auto const* part : {&key.ownerType, &key.ownerId, &key.reference, &key.relation, &key.peerType, &key.peerId})
		result += std::to_string(part->size()) + ":" + *part;
	return result;
}
std::vector<StationMonitorMetric> const& stationMonitorMetricCatalogue(std::string_view type)
{
	static std::vector<StationMonitorMetric> const station{
		{"reserveExhausted", "boolean"}, {"backupEngaged", "boolean"},
		{"dispatchShortfallKW", "kW"}, {"protectedUnmetKW", "kW"}};
	static std::vector<StationMonitorMetric> const reservoirs{{"temperatureC", "°C"}};
	if (type == "station") return station;
	if (type == "thermal-reservoir" || type == "hull") return reservoirs;
	static std::vector<StationMonitorMetric> const stores{
		{"usableFraction", "fraction"}, {"totalDissolved", "mg/L"},
		{"maxContaminantConcentration", "mg/L"}, {"unexpectedSpecies", "boolean"}};
	static std::vector<StationMonitorMetric> const connections{
		{"lossFraction", "fraction"}, {"lossRate", "unit/s"}, {"reversed", "boolean"},
		{"utilizationFraction", "fraction"}, {"totalDissolved", "mg/L"},
		{"maxContaminantConcentration", "mg/L"}, {"unexpectedSpecies", "boolean"}};
	static std::vector<StationMonitorMetric> const atmosphere = [] {
		std::vector<StationMonitorMetric> result;
		for (auto const& metric : stationAtmosphereMetricCatalogue()) result.push_back({metric.name, metric.unit});
		return result;
	}();
	static std::vector<StationMonitorMetric> const loops{{"fillFraction", "fraction"}, {"circulationFraction", "fraction"}, {"loopTemperatureC", "°C"}, {"condition", "fraction"}};
	static std::vector<StationMonitorMetric> const chillers{{"coolingUnmetFraction", "fraction"}, {"sinkOverMaxCondensing", "boolean"}, {"refrigerantFraction", "fraction"}};
	static std::vector<StationMonitorMetric> const heatSources{
		{"decayFraction", "fraction"}, {"decayedOutputKW", "kW"}, {"deliveredOutputKW", "kW"}};
	static std::vector<StationMonitorMetric> const engines{{"tripped", "boolean"}, {"hotHeadroomC", "°C"},
		{"electricalOutputKW", "kW"}, {"rejectionKW", "kW"}};
	static std::vector<StationMonitorMetric> const radiators{{"remainingAreaFraction", "fraction"}, {"reservoirTemperatureC", "°C"}};
	static std::vector<StationMonitorMetric> const batteries{{"stateOfCharge", "fraction"}, {"dischargeShortfallFraction", "fraction"}, {"wearFraction", "fraction"}};
	static std::vector<StationMonitorMetric> const generators{{"outputKW", "kW"}, {"outputDeviationFraction", "fraction"}, {"sinkTemperatureC", "°C"}, {"startupUnmetKW", "kW"}};
	static std::vector<StationMonitorMetric> const solar{{"availabilityFactor", "fraction"}, {"availableFraction", "fraction"}};
	static std::vector<StationMonitorMetric> const machines{{"unmetFraction", "fraction"}};
	static std::vector<StationMonitorMetric> const microbial{{"unmetFraction", "fraction"}, {"condition", "fraction"}};
	static std::vector<StationMonitorMetric> const biology{{"biomassFraction", "fraction"}, {"unmetResourceFraction", "fraction"}};
	static std::vector<StationMonitorMetric> const fixtures{
		{"lightShortfallFraction", "fraction"}, {"requestedElectricalKW", "kW"},
		{"suppliedElectricalKW", "kW"}, {"deliveredLightKW", "kW"}, {"lumens", "lm"}};
	if (type == "lighting-fixture") return fixtures;
	if (type == "microbial-processor") return microbial;
	if (type == "water-treatment" || type == "electrolyzer" || type == "co2-capture"
		|| type == "sabatier" || type == "methane-processor" || type == "fuel-cell") return machines;
	if (type == "crop" || type == "algae") return biology;
	if (type == "battery") return batteries;
	if (type == "generator") return generators;
	if (type == "solar-array") return solar;
	if (type == "coolant-loop") return loops;
	if (type == "chiller") return chillers;
	if (type == "heat-source") return heatSources;
	if (type == "heat-engine") return engines;
	if (type == "radiator") return radiators;
	static std::vector<StationMonitorMetric> const empty;
	return type == "store" ? stores : type == "connection" ? connections : type == "atmosphere" ? atmosphere : empty;
}
bool stationMonitorSubjectExists(StationSystemsSettings const& settings, std::string_view type, std::string_view id)
{
	if (type == "station") return id == "station";
	if (type == "hull") return id == "hull" && settings.hull.has_value();
	if (type == "lighting-fixture") return findId(settings.lightingFixtures, id) != settings.lightingFixtures.end();
	if (type == "thermal-reservoir") return findId(settings.thermalReservoirs, id) != settings.thermalReservoirs.end();
	if (type == "water-treatment") return findId(settings.waterTreatments, id) != settings.waterTreatments.end();
	if (type == "electrolyzer") return findId(settings.electrolyzers, id) != settings.electrolyzers.end();
	if (type == "co2-capture") return findId(settings.co2Captures, id) != settings.co2Captures.end();
	if (type == "sabatier") return findId(settings.sabatierReactors, id) != settings.sabatierReactors.end();
	if (type == "methane-processor") return findId(settings.methaneProcessors, id) != settings.methaneProcessors.end();
	if (type == "fuel-cell") return findId(settings.fuelCells, id) != settings.fuelCells.end();
	if (type == "microbial-processor") return findId(settings.microbialProcessors, id) != settings.microbialProcessors.end();
	if (type == "crop") return findId(settings.cropPopulations, id) != settings.cropPopulations.end();
	if (type == "algae") return findId(settings.algaeCultures, id) != settings.algaeCultures.end();
	if (type == "battery") return findId(settings.batteries, id) != settings.batteries.end();
	if (type == "generator") return findId(settings.generators, id) != settings.generators.end();
	if (type == "solar-array") return findId(settings.solarArrays, id) != settings.solarArrays.end();
	if (type == "coolant-loop") return findId(settings.coolantLoops, id) != settings.coolantLoops.end();
	if (type == "chiller") return findId(settings.chillers, id) != settings.chillers.end();
	if (type == "heat-source") return findId(settings.heatSources, id) != settings.heatSources.end();
	if (type == "heat-engine") return findId(settings.heatEngines, id) != settings.heatEngines.end();
	if (type == "radiator") return findId(settings.radiators, id) != settings.radiators.end();
	if (type == "atmosphere") return findId(settings.atmospheres, id) != settings.atmospheres.end();
	if (type == "store") return findId(settings.stores, id) != settings.stores.end();
	if (type == "connection")
		for (auto const& link : build(settings).links)
			if (stationConnectionMonitorSubjectId(link.designKey) == id) return true;
	return false;
}
}

namespace station
{
	std::string_view stationMonitorProvenanceTag(MonitorProvenance provenance)
	{
		switch (provenance)
		{
		case MonitorProvenance::Iss: return "iss";
		case MonitorProvenance::Who: return "who";
		case MonitorProvenance::Rated: return "rated";
		case MonitorProvenance::Provisional: return "prov";
		case MonitorProvenance::Custom: break;
		}
		return "custom";
	}

}

namespace station
{
	std::string_view stationAlertSeverityTag(AlertSeverity severity)
	{
		switch (severity)
		{
		case AlertSeverity::Advisory: return "Advisory";
		case AlertSeverity::Warning: return "Warning";
		case AlertSeverity::Critical: return "Critical";
		}
		return "Warning";
	}
	std::string_view stationAlertDirectionTag(AlertDirection direction)
	{
		return direction == AlertDirection::Above ? "Above" : "Below";
	}
}
