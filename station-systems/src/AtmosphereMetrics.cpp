#include "station/AtmosphereMetrics.h"
#include <algorithm>
#include <cmath>
namespace station
{
namespace {
	double toxicConcentration(StationAtmosphereSnapshot const& compartment) noexcept
	{
		return compartment.volumeM3 > 0.0 && std::isfinite(compartment.toxicContaminantKg)
			? compartment.toxicContaminantKg * 1000.0 / compartment.volumeM3
			: 0.0;
	}
}
	std::vector<StationAtmosphereMetric> const& stationAtmosphereMetricCatalogue()
	{
		static std::vector<StationAtmosphereMetric> const catalogue{
			{"oxygenPartialPressureKPa", "kPa", [](auto const& c) noexcept { return c.oxygenPartialPressureKPa; }},
			{"carbonDioxidePartialPressureKPa", "kPa", [](auto const& c) noexcept { return c.carbonDioxidePartialPressureKPa; }},
			{"pressureKPa", "kPa", [](auto const& c) noexcept { return c.pressureKPa; }},
			{"temperatureC", "°C", [](auto const& c) noexcept { return c.temperatureC; }},
			{"relativeHumidity", "fraction", [](auto const& c) noexcept { return c.relativeHumidity; }},
			{"toxicConcentration", "g/m³", toxicConcentration},
			{"oxygenUnmetKg", "kg/tick", [](auto const& c) noexcept { return c.lastOxygenUnmetKg; }},
			{"leakRateKgPerS", "kg/s", [](auto const& c) noexcept { return c.leakRateKgPerS; }}};
		return catalogue;
	}

	StationAtmosphereMetric const* findStationAtmosphereMetric(std::string_view metric)
	{
		auto const& catalogue = stationAtmosphereMetricCatalogue();
		auto const found = std::find_if(catalogue.begin(), catalogue.end(),
			[&](auto const& entry) { return entry.name == metric; });
		return found == catalogue.end() ? nullptr : &*found;
	}

}
