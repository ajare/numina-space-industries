#pragma once
#include "station/Configuration.h"
namespace station
{
	// Runtime-only Cell observation, never authored or serialized. Coordinates
	// identify geometry; sectorUid prevents stale values colouring a new Sector.
	struct StationCellAtmosphereSnapshot {
		std::size_t level{}, gridIndex{};
		std::int64_t sectorUid{};
		std::array<double,4> massKg{};
		double pressureKPa{}, temperatureC{}, relativeHumidity{}, velocityX{}, velocityY{};
		bool resolvedGas{};
		double densityKgPerM3{},windSpeedMS{},dynamicPressurePa{};
		std::array<double,2> pressureGradientPaPerM{},dragForceN{},pressureForceN{},forceN{};
		// Force fields refer to stationary A=1 m2, V=1 m3, Cd=1; never actual
		// placed objects. Disabled/unavailable zeros are not measurements.
		bool windEnabled{},forcesEnabled{},windAvailable{},forceAvailable{};
		bool operator==(StationCellAtmosphereSnapshot const&) const = default;
	};
	struct StationAtmosphereSnapshot
	{
		std::vector<StationFaultSnapshot> activeFaults;
		double fireOxygenConsumedKg{0.0}, fireCarbonDioxideKg{0.0};
		// Retained CO hazard inventory, distinct from the four pathway gases.
		double carbonMonoxideKg{0.0}, fireHeatKJ{0.0};
		// Liquid spill inventory, separate from the gas/condensation ledger.
		double spilledCoolantKg{0.0}, spilledCoolantEnergyKJ{0.0};
		// #845: toxic-species contamination retained from a battery
		// thermal-runaway booking. The contaminant is a retained hazard
		// ledger beside the spilled-coolant one: it is not a breathing
		// gas, joins no partial pressure and moves through no pathway in
		// this slice - it is booked mass that the compartment keeps, and
		// the closure audit requires it to equal exactly what the banks
		// that declared this compartment gave up. Crew health reaction is
		// out of scope; the ledger closing is the point.
		double toxicContaminantKg{0.0};
		// One-shot hazard heat booked into this compartment's thermal
		// state (#845): the cumulative total that enters the
		// compartment's energy ledger, exactly as the reservoirs carry
		// their trip-spike hazard energy.
		double hazardHeatKJ{0.0};
		double lastHazardHeatKW{0.0};
		std::string id;
		std::string reservoirId;      // empty: adiabatic, no thermal coupling
		double volumeM3{0.0};
		double thermalCapacityKJPerK{0.0};
		double startingThermalCapacityKJPerK{0.0};
		bool inventoryThermodynamics{false};
		// Spatial views: temperature is capacity-weighted, pressure/partials/RH
		// volume-weighted. They are not an equilibrated compartment EOS state.
		bool spatial{false}, resolvedGas{true};
		double minCellPressureKPa{}, maxCellPressureKPa{}, kineticEnergyKJ{};
		// Sensible internal energy carried by non-pathway inventory changes.
		double inventoryEnergyKJ{0.0};
		double exchangerConductanceKWPerK{0.0};
		int occupants{0};
		double oxygenUsePerOccupantKgPerS{0.0};
		double co2ProductionPerOccupantKgPerS{0.0};
		double sensibleHeatPerOccupantKW{0.0};
		double moistureAdditionPerOccupantKgPerS{0.0};
		double leakRateKgPerS{0.0};
		double minPressureKPa{0.0};
		double maxPressureKPa{0.0};
		double minOxygenPartialPressureKPa{0.0};
		double maxCarbonDioxidePartialPressureKPa{0.0};
		// Initial versus current conditions.
		double startingOxygenKg{0.0};
		double startingNitrogenKg{0.0};
		double startingCarbonDioxideKg{0.0};
		double startingWaterVapourKg{0.0};
		double startingTemperatureC{0.0};
		double startingPressureKPa{0.0}; // derived from the starting inventory
		double oxygenKg{0.0};
		double nitrogenKg{0.0};
		double carbonDioxideKg{0.0};
		double waterVapourKg{0.0};
		double temperatureC{0.0};
		// Derived pressures (kPa); independent volume-weighted Cell means in
		// spatial views, otherwise the equilibrated inventory/temperature EOS.
		double pressureKPa{0.0};       // absolute total, sum of partials
		double oxygenPartialPressureKPa{0.0};
		double nitrogenPartialPressureKPa{0.0};
		double carbonDioxidePartialPressureKPa{0.0};
		double waterVapourPartialPressureKPa{0.0};
		double saturationVapourPressureKPa{0.0};
		double relativeHumidity{0.0};  // vapour partial / saturation, <= 1
		// Last tick flows.
		double lastOxygenDemandKg{0.0};
		double lastOxygenConsumedKg{0.0};
		double lastOxygenUnmetKg{0.0}; // demand the inventory could not supply
		double lastCo2ProducedKg{0.0};
		double lastVapourAddedKg{0.0};
		double lastVapourCondensedKg{0.0};
		double lastLeakedKg{0.0};
		double lastCollectedKg{0.0}; // vapour removed by water recovery this tick
		double lastElectrolysisOxygenKg{0.0}; // O2 delivered into this compartment this tick (#404)
		double lastCo2CapturedKg{0.0}; // CO2 removed by capture this tick (#404)
		double lastCycleRemovedKg{0.0};
		double lastCycleRecoveredKg{0.0};
		double lastCycleLostKg{0.0};
		double lastOccupantHeatKW{0.0};
		double lastLatentHeatKW{0.0};
		double lastExchangeIntoCompartmentKW{0.0};
		// Conditions: reported, never clamped.
		bool abovePressureLimit{false};
		bool belowPressureLimit{false};
		bool lowOxygen{false};
		bool highCarbonDioxide{false};
		bool condensing{false};
		bool thermallyCoupled{false};
		// Metabolic heat delivered into this compartment by authored crews
		// this tick and cumulatively (#401). Crew heat is booked as its
		// own thermal term - never folded into the compartment's own
		// occupancy heat - and the thermal ledger closes over it.
		double lastCrewHeatKW{0.0};
		double crewHeatEnergyKJ{0.0};
		// Crop heat delivered into this compartment this tick and
		// cumulatively (#409): the lamp energy not fixed as biomass
		// chemical energy plus the respiration heat, booked as its own
		// thermal term beside the crew heat and entering the same coupled
		// vapour/thermal solve and thermal ledger.
		double lastCropHeatKW{0.0};
		double cropHeatEnergyKJ{0.0};
		double lastFixtureHeatKW{}, fixtureHeatEnergyKJ{};
		// Algae culture heat delivered into this compartment this tick and
		// cumulatively (#411): booked as its own thermal term beside the
		// crop heat - the two populations never share a heat pathway.
		double lastAlgaeHeatKW{0.0};
		double algaeHeatEnergyKJ{0.0};
		// Cumulative pathway ledger: every gram that moved, by pathway.
		double oxygenConsumedKg{0.0};
		double co2ProducedKg{0.0};
		double vapourAddedKg{0.0};
		double condensedKg{0.0};
		double leakedKg{0.0};
		// Cumulative loss through exterior pathways, in StationPathwayGases order.
		// Separate from constant leaks and airlock losses; never counted twice.
		std::array<double, 4> exteriorLostKg{};
		std::array<double, 4> combustionKg{}; // signed O2/N2/CO2/vapour inlet/exhaust exchange
		double collectedKg{0.0}; // cumulative vapour removed by water recovery
		double electrolysisOxygenKg{0.0}; // cumulative O2 added by electrolysis (#404)
		double co2CapturedKg{0.0}; // cumulative CO2 removed by capture (#404)
		double recoveredKg{0.0};
		double lostKg{0.0};
		double connectionCarbonDioxideLostKg{0.0};
		double connectionWaterVapourLostKg{0.0};
		double startingEnergyKJ{0.0};
		double currentEnergyKJ{0.0};
		double occupantHeatEnergyKJ{0.0};
		double latentHeatEnergyKJ{0.0};
		double exchangedEnergyKJ{0.0};
		// Heat conducted into (+) or out of (-) this compartment through
		// its Thermal links (#794), last-tick rate and cumulative total.
		// Booked once per side as the exact negation of the far side, so
		// both compartments' ledgers close on one number.
		double lastConductedKW{0.0};
		double conductedEnergyKJ{0.0};
		// Advected enthalpy (#802): the thermal energy this compartment's
		// gas has carried into or away from it across its Atmosphere
		// pathways since configuration, signed into the compartment
		// (+ gains arriving heat, - loses heat riding out with its own
		// escaping gas), booked once per side as the exact negation of
		// the far side of each inter-compartment transfer. Venting to
		// Exterior vacuum books the carried energy as an explicit loss
		// beside exteriorLostKg, never a silent disappearance. Reported
		// separately from conducted heat so what a hatch did stays
		// distinct from what a wall did.
		double lastAdvectedKW{0.0};
		double advectedEnergyKJ{0.0};
		double residualKJ{0.0}; // supplied energy incl. inventory minus internal and spatial kinetic energy
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationAtmosphereSnapshot const&) const = default;
	};
	// #867 (spec §3.1): the Metrics an `atmosphere` Monitor subject publishes.
	// `read` resolves the value from the compartment the Station step already
	// published; the derived Metrics compute at read time and store nothing.
	// This is the single source of truth for which Metric names are valid for
	// the subject type: validation, evaluation and surfaces all resolve here.
	struct StationAtmosphereMetric
	{
		std::string_view name;
		std::string_view unit;
		double (*read)(StationAtmosphereSnapshot const&) noexcept;
	};
	std::vector<StationAtmosphereMetric> const& stationAtmosphereMetricCatalogue();
	StationAtmosphereMetric const* findStationAtmosphereMetric(std::string_view metric);
}
