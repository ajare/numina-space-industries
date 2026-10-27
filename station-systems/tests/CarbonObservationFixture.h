#pragma once
#include "station/StationSystems.h"

namespace station_test
{
inline station::StationSystemsSettings carbonObservationFixture()
{
	station::StationSystemsSettings s; s.enabled = true; s.observationIntervalSeconds = 0.1f;
	station::SolarArraySettings solar; solar.id = "sun"; solar.capacityKW = 100; s.solarArrays = {solar};
	for (auto id : {"co2", "hydrogen", "methane", "water", "carbon", "cell-h2", "cell-o2", "cell-water", "breathing-oxygen"}) {
		station::ResourceStoreSettings stock; stock.id = id; stock.unit = "kg";
		stock.capacity = 100; stock.startingQualified = 10;
		if (stock.id == "breathing-oxygen") stock.materialId = "oxygen";
		s.stores.push_back(stock);
	}
	station::ThermalReservoirSettings sink; sink.id = "sink"; sink.thermalClass = "moderate";
	sink.heatCapacityKJPerK = 100000; sink.startingTemperatureC = 20; s.thermalReservoirs = {sink};
	station::SabatierReactorSettings r; r.id = "reactor";	r.co2StoreId = "co2"; r.hydrogenStoreId = "hydrogen"; r.methaneStoreId = "methane";
	r.waterStoreId = "water"; r.heatSinkId = "sink"; r.thermalClass = "moderate"; r.rateKgPerS = 0.001;
	s.sabatierReactors = {r};
	station::MethaneProcessorSettings p; p.id = "processor";	p.methaneStoreId = "methane"; p.hydrogenStoreId = "hydrogen"; p.carbonStoreId = "carbon";
	p.heatSinkId = "sink"; p.thermalClass = "moderate"; p.rateKgPerS = 0.001; s.methaneProcessors = {p};
	station::FuelCellSettings c; c.id = "cell";	c.hydrogenStoreId = "cell-h2"; c.oxygenStoreId = "cell-o2"; c.waterStoreId = "cell-water";
	c.heatSinkId = "sink"; c.thermalClass = "moderate"; c.ratedOutputKW = 5; c.commandedOutputKW = 2;
	s.fuelCells = {c};
	return s;
}
}
