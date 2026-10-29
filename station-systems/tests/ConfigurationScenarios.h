#pragma once
// Established pathway fixture and refusal matrix, shared with the
// World integration checks; no private or application headers are required.
#include "station/Configuration.h"
#include <limits>
namespace station_test
{
inline station::StationSystemsSettings pathwaySettings()
{
	station::StationSystemsSettings s;
	s.enabled = true;
	station::AtmosphereSettings a;
	a.id = "a"; a.volumeM3 = 100; a.thermalCapacityKJPerK = 10000;
	a.startingTemperatureC = 0; // Kelvin regression: zero Celsius is not vacuum.
	a.minPressureKPa = 1; a.maxPressureKPa = 200;
	a.startingOxygenKg = 30; a.startingNitrogenKg = 80;
	a.startingCarbonDioxideKg = 0.6; a.startingWaterVapourKg = 0.1;
	auto b = a; b.id = "b";
	b.startingOxygenKg = 10; b.startingNitrogenKg = 40;
	b.startingCarbonDioxideKg = 0.2; b.startingWaterVapourKg = 0.02;
	s.atmospheres = {a, b};
	s.atmospherePathways = {{"opening", "a", "b", 1.0, 1.0}};
	return s;
}

template<class Refuse>
void invalidPathways(station::StationSystemsSettings const& s, Refuse&& refuse)
{
	for (double value : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		auto bad = s; bad.atmospherePathways[0].areaM2 = value; refuse(bad, "areaM2");
	}
	for (double value : {-0.01, 1.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		auto bad = s; bad.atmospherePathways[0].openFraction = value; refuse(bad, "openFraction");
	}
	for (double value : {0.0,-1.0,1.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
		auto bad=s;bad.atmospherePathways[0].dischargeCoefficient=value;refuse(bad,"dischargeCoefficient");
	}
	for (bool first : {false, true})
	{
		auto bad = s;
		(first ? bad.atmospherePathways[0].aAtmosphereId : bad.atmospherePathways[0].bAtmosphereId) = "not-a-compartment";
		refuse(bad, "not-a-compartment");
	}
	{ auto bad = s; bad.atmospherePathways[0].bAtmosphereId = "a"; refuse(bad, "self-loop"); }
	{ auto bad = s; bad.atmospherePathways.push_back(bad.atmospherePathways[0]); refuse(bad, "duplicated"); }
}
}
