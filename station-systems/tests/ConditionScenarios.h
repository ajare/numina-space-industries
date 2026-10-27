#pragma once
#include "station/StationSystemsRuntime.h"
#include <limits>
#include <stdexcept>

namespace station_test::condition
{
inline void require(bool ok, char const* message)
{ if (!ok) throw std::runtime_error(message); }

inline station::StationSystemsSettings conditionStation()
{
	station::StationSystemsSettings s;
	s.enabled = true; s.systemsStepWorldTicks = 1;
	station::ThermalReservoirSettings sink;
	sink.id = "sink"; sink.thermalClass = "moderate";
	sink.heatCapacityKJPerK = 10000; sink.startingTemperatureC = 20;
	s.thermalReservoirs = {sink};
	station::CoolantLoopSettings loop;
	loop.id = "loop"; loop.reservoirId = "sink";
	loop.coolantCapacityKg = loop.startingCoolantKg = 100;
	loop.specificHeatKJPerKgK = 4; loop.startingTemperatureC = 20;
	loop.ratedFlowKgPerS = 10; loop.pumpPowerKW = 1;
	loop.condition = station::InstallationConditionSettings{};
	loop.condition->seizureThreshold = 0.15;
	loop.condition->restoreStoreId = "stock";
	loop.condition->restorePerHour = 360; // 0.1 point/s: deliberately accelerated fixture
	loop.condition->stockUnitsPerConditionPoint = 10;
	s.coolantLoops = {loop};
	station::ResourceStoreSettings stock;
	stock.id = "stock"; stock.unit = "kg";
	stock.capacity = stock.startingQualified = 10; stock.protectedReserve = 2;
	s.stores = {stock};
	station::SolarArraySettings solar;
	solar.id = "solar"; solar.capacityKW = 10;
	s.solarArrays = {solar};
	return s;
}
inline void conditionStress()
{
	station::ScopedStationStepRatioForTesting clock(std::nullopt);
	for (bool critical : {false, true})
	{
		auto s = conditionStation(); auto& loop = s.coolantLoops[0];
		loop.criticalService = critical;
		loop.condition->overheatStressC = 10;
		loop.condition->overheatDegradePerHour = 360;
		station::StationSystemsRuntime run(s, 0.25f);
		require(run.advanceTo(2), "Condition degradation failed audit");
		auto snap = run.snapshot();
		require(std::abs(snap.coolantLoops[0].condition - 0.8) < 1e-12
			&& snap.coolantLoops[0].conditionStressors == "overheat"
			&& std::abs(snap.coolantLoops[0].flowKgPerS - 8) < 1e-10
			&& snap.coolantLoops[0].conditionCapability == snap.coolantLoops[0].condition,
			"Condition did not continuously scale delivered capability");
		require(run.updateNode<station::CoolantLoopSettings>("loop", [](auto& l)
			{ l.condition->overheatStressC = 100; }), "stress removal refused");
		require(run.advanceTo(4) && run.snapshot().coolantLoops[0].condition == snap.coolantLoops[0].condition
			&& run.snapshot().coolantLoops[0].conditionStressors.empty(), "removed stress kept degrading");
	}
	// Cavitation must still act after a coolant-leak, but not on an isolated pump.
	auto s = conditionStation();
	s.coolantLoops[0].startingCoolantKg = 20;
	s.coolantLoops[0].condition->cavitationDegradePerHour = 360;
	station::StationSystemsRuntime run(s, 0.25f);
	require(run.advanceTo(1) && run.snapshot().coolantLoops[0].conditionStressors == "cavitation",
		"low-charge running pump did not cavitate");
	station::InstallationOutageSettings outage;
	outage.id = "isolate"; outage.installationId = "loop"; outage.capacityFraction = 0;
	outage.firstOccurrenceSeconds = 0; outage.durationSeconds = 10;
	s.outages = {outage};
	station::StationSystemsRuntime idle(s, 0.25f);
	require(idle.advanceTo(1) && idle.snapshot().coolantLoops[0].condition == 1,
		"isolated pump cavitated");
}
inline void conditionCrossing()
{
	station::ScopedStationStepRatioForTesting clock(std::nullopt);
	auto s = conditionStation();
	auto& model = *s.coolantLoops[0].condition;
	model.initialCondition = 0.2; model.overheatStressC = 10; model.overheatDegradePerHour = 360;
	station::StationSystemsRuntime run(s, 0.25f);
	require(run.advanceTo(1), "seizure crossing failed audit");
	auto snap = run.snapshot(); auto const& loop = snap.coolantLoops[0];
	require(loop.activeFaults.size() == 1 && loop.activeFaults[0].modeId == "pump-seizure"
		&& loop.activeFaults[0].causeKind == station::FaultCause::Kind::DegradationCrossing
		&& loop.activeFaults[0].onsetTick <= snap.worldTick
		&& loop.activeFaults[0].derivedImpact == 1 && loop.availabilityFactor == 0 && loop.flowKgPerS == 0,
		"crossing did not spawn an attributed, service-stopping seizure");
	require(run.advanceTo(5), "post-seizure drive failed");
	require(std::count_if(run.events().begin(), run.events().end(), [](auto const& e)
		{ return e.category == "fault"; }) == 1, "seizure onset duplicated");
	// Distinct modes coexist: a leak may precede a degradation crossing.
	station::AtmosphereSettings room;
	room.id = "room"; room.volumeM3 = 100; room.thermalCapacityKJPerK = 10000;
	room.startingTemperatureC = 20; room.minPressureKPa = 1; room.maxPressureKPa = 200;
	s.atmospheres = {room}; s.coolantLoops[0].coolantLeak = station::CoolantLeakSettings{"room", 40};
	station::StationSystemsRuntime both(s, 0.25f);
	require(both.injectFault("loop", "coolant-leak", {}) && both.advanceTo(1)
		&& both.snapshot().coolantLoops[0].activeFaults.size() == 2
		&& both.snapshot().coolantLoops[0].flowKgPerS == 0,
		"leak suppressed the independent pump-seizure mode");
}
inline void conditionRestore()
{
	station::ScopedStationStepRatioForTesting clock(std::nullopt);
	auto s = conditionStation(); s.coolantLoops[0].condition->initialCondition = 0.5;
	s.stores[0].startingQualified = 3; // only one unit above reserve: 0.1 point funded
	station::StationSystemsRuntime run(s, 0.25f);
	auto before = run.snapshot(); auto events = run.events(); std::string diagnostic;
	for (double target : {0.5, -1., 2., std::numeric_limits<double>::quiet_NaN()})
		require(!run.restoreCondition("loop", target, &diagnostic) && !diagnostic.empty(), "invalid restore accepted");
	require(!run.restoreCondition("missing", 1, &diagnostic)
		&& run.snapshot() == before && run.events() == events && !run.hasFaultCommands(),
		"refused restore mutated runtime");
	require(run.restoreCondition("loop", 1, &diagnostic) && diagnostic.empty()
		&& run.snapshot() == before && run.events() == events, "restore healed before tick boundary");
	require(run.advanceTo(0.25), "funded recovery failed audit");
	auto snap = run.snapshot();
	require(std::abs(snap.coolantLoops[0].condition - 0.525) < 1e-12
		&& snap.coolantLoops[0].restoring && snap.stores[0].currentQualified == 2.75,
		"restore was not rate-bounded and stock-funded");
	require(run.advanceTo(3), "stock-limited recovery failed audit");
	snap = run.snapshot();
	require(std::abs(snap.coolantLoops[0].condition - 0.6) < 1e-12
		&& snap.stores[0].currentQualified == 2 && !snap.coolantLoops[0].restoring
		&& std::abs(snap.coolantLoops[0].restoreStockConsumedUnits - 1) < 1e-12,
		"recovery consumed reserve or healed for free");
	require(!run.restoreCondition("loop", 1, &diagnostic) && diagnostic.find("reserve") != std::string::npos,
		"unfunded restore was not explained");
	// Recovery under a stressor waits without spending stock.
	s = conditionStation(); s.coolantLoops[0].condition->initialCondition = 0.5;
	s.coolantLoops[0].condition->overheatStressC = 10;
	s.coolantLoops[0].condition->overheatDegradePerHour = 36;
	station::StationSystemsRuntime stressed(s, 0.25f);
	require(stressed.restoreCondition("loop", 0.6) && stressed.advanceTo(1)
		&& stressed.snapshot().stores[0].currentQualified == 10, "stressed restore spent stock");
	auto const stressedCondition = stressed.snapshot().coolantLoops[0].condition;
	require(stressed.updateNode<station::CoolantLoopSettings>("loop", [](auto& l) { l.condition->overheatDegradePerHour = 0; })
		&& stressed.advanceTo(2) && stressed.snapshot().stores[0].currentQualified == 10
		&& stressed.snapshot().coolantLoops[0].condition == stressedCondition,
		"zero-rate active stressor allowed recovery");
	require(stressed.updateNode<station::CoolantLoopSettings>("loop", [](auto& l) { l.condition->overheatStressC = 100; })
		&& stressed.advanceTo(4), "benign restore failed");
	require(stressed.snapshot().coolantLoops[0].condition == 0.6
		&& stressed.snapshot().coolantLoops[0].restoreTargetCondition == 0,
		"recovery did not stop at target");
}
inline void conditionTransactions()
{
	station::ScopedStationStepRatioForTesting clock(std::nullopt);
	auto s = conditionStation();
	s.coolantLoops[0].condition->initialCondition = 0.5;
	station::StationSystemsRuntime run(s, 0.25f);
	auto before = run.snapshot(); auto events = run.events();
	require(run.restoreCondition("loop", 1), "rollback restore refused");
	require(run.updateNode<station::CoolantLoopSettings>("loop", [](auto& l)
		{ l.specificHeatKJPerKgK = std::numeric_limits<double>::infinity(); }), "audit poison refused");
	require(!run.advanceTo(1) && !run.snapshot().healthy, "poisoned recovery passed audit");
	auto after = run.snapshot();
	require(after.coolantLoops[0].condition == before.coolantLoops[0].condition
		&& after.coolantLoops[0].restoreStockConsumedUnits == 0
		&& after.stores[0].currentQualified == before.stores[0].currentQualified
		&& run.events() == events, "failed tick published recovery or spent stock");
	// Two accepted jobs cannot double-spend shared stock, even within one step.
	s.stores[0].startingQualified = 2.1;
	auto second = s.coolantLoops[0]; second.id = "second"; s.coolantLoops.push_back(second);
	station::StationSystemsRuntime shared(s, 0.25f);
	require(shared.restoreCondition("loop", 1) && shared.restoreCondition("second", 1)
		&& shared.advanceTo(1), "shared stock recovery failed audit");
	after = shared.snapshot();
	require(after.stores[0].currentQualified == 2
		&& std::abs(after.coolantLoops[0].restoreStockConsumedUnits
			+ after.coolantLoops[1].restoreStockConsumedUnits - 0.1) < 1e-12,
		"shared recovery double-spent reserve-protected stock");
	for (uint32_t ratio : {1u, 6u, 60u})
	{
		auto offered = conditionStation(); offered.systemsStepWorldTicks = ratio;
		offered.coolantLoops[0].condition->overheatStressC = 10;
		offered.coolantLoops[0].condition->overheatDegradePerHour = 360;
		station::StationSystemsRuntime stepped(offered, 1.f / 60.f);
		require(stepped.advanceTo(2), "Condition step-ratio drive failed");
		auto snap = stepped.snapshot();
		require(std::abs(snap.coolantLoops[0].condition - (1 - 0.1 * snap.elapsedSeconds)) < 1e-10,
			"constant-stressor Condition did not integrate elapsed seconds");
	}
}
}
