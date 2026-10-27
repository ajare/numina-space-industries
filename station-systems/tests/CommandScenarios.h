#pragma once
#include "station/StationSystemsRuntime.h"
#include <limits>
#include <stdexcept>

namespace station_test::commands
{
inline void require(bool ok, char const* message)
{ if (!ok) throw std::runtime_error(message); }

inline station::StationSystemsSettings punctureStation()
{
	station::StationSystemsSettings s;
	s.enabled = true; s.systemsStepWorldTicks = 1;
	station::ThermalReservoirSettings reservoir;
	reservoir.id = "sink"; reservoir.thermalClass = "moderate";
	reservoir.heatCapacityKJPerK = 10000;
	reservoir.startingTemperatureC = 20;
	s.thermalReservoirs = {reservoir};
	station::CoolantLoopSettings loop;
	loop.id = "loop"; loop.reservoirId = "sink";
	loop.coolantCapacityKg = loop.startingCoolantKg = 100;
	loop.specificHeatKJPerKgK = 4; loop.startingTemperatureC = 20;
	loop.ratedFlowKgPerS = 10; loop.pumpPowerKW = 1;
	loop.coolantLeak = station::CoolantLeakSettings{"room", 40};
	s.coolantLoops = {loop};
	station::AtmosphereSettings room;
	room.id = "room"; room.volumeM3 = 100; room.thermalCapacityKJPerK = 10000;
	room.startingTemperatureC = 20; room.minPressureKPa = 1; room.maxPressureKPa = 200;
	s.atmospheres = {room};
	station::SolarArraySettings solar;
	solar.id = "solar"; solar.capacityKW = 10;
	s.solarArrays = {solar};
	return s;
}
inline station::StationSystemsSettings repairStation()
{
	auto s = punctureStation();
	station::ResourceStoreSettings parts;
	parts.id = "parts"; parts.unit = "kg"; parts.capacity = 100; parts.startingQualified = 10;
	station::ResourceStoreSettings waste;
	waste.id = "waste"; waste.unit = "kg"; waste.capacity = 100; waste.startingQualified = 0;
	s.stores = {parts, waste};
	station::RepairPartsSettings wiring;
	wiring.partsStoreId = "parts";
	wiring.repairs = {
		station::FaultRepairSettings{"coolant-leak", 10.0, 2.0, "waste"},
		station::FaultRepairSettings{"pump-seizure", 20.0, 4.0, "waste"}};
	s.coolantLoops[0].repairParts = wiring;
	return s;
}
inline int countCategory(station::StationSystemsRuntime const& run, std::string const& category)
{
	auto const events = run.events();
	return static_cast<int>(std::count_if(events.begin(), events.end(),
		[&](auto const& e) { return e.category == category; }));
}
inline bool sawMessage(station::StationSystemsRuntime const& run, std::string const& needle)
{
	auto const events = run.events();
	return std::any_of(events.begin(), events.end(), [&](auto const& e)
	{
		return e.message.find(needle) != std::string::npos;
	});
}
inline const station::StationMaintenanceSnapshot* findJob(station::StationSystemsSnapshot const& snap, std::string const& id)
{
	auto const it = std::find_if(snap.maintenanceJobs.begin(), snap.maintenanceJobs.end(),
		[&](auto const& job) { return job.id == id; });
	return it == snap.maintenanceJobs.end() ? nullptr : &*it;
}
inline const station::StationSystemsStoreSnapshot* findStoreSnap(station::StationSystemsSnapshot const& snap, std::string const& id)
{
	auto const it = std::find_if(snap.stores.begin(), snap.stores.end(),
		[&](auto const& store) { return store.id == id; });
	return it == snap.stores.end() ? nullptr : &*it;
}
inline void faultOutages()
{
	station::ScopedStationStepRatioForTesting clock(std::nullopt);
	for (double cap : {0.0, 0.3, 0.8})
	{
		auto settings = punctureStation();
		station::InstallationOutageSettings outage;
		outage.id = "outage"; outage.installationId = "loop"; outage.capacityFraction = cap;
		outage.firstOccurrenceSeconds = 0; outage.durationSeconds = 2;
		settings.outages = {outage};
		station::StationSystemsRuntime run(settings, 0.25f);
		require(run.injectFault("loop", "coolant-leak", {}) && run.advanceTo(1), "outaged leak failed");
		auto const loop = run.snapshot().coolantLoops[0];
		require(loop.availabilityFactor == std::min(cap, 0.6)
			&& std::abs(loop.flowKgPerS - 10 * std::min(cap, 0.6)) < 1e-10,
			"fault and Outage multiplied instead of deepest-wins");
		require(run.advanceTo(3) && run.snapshot().coolantLoops[0].availabilityFactor == 0.6,
			"Outage restoration cleared the fault cap");
	}
	for (double mass : {100.0, 200.0})
	{
		auto settings = punctureStation(); settings.coolantLoops[0].coolantLeak->releasedKg = mass;
		station::StationSystemsRuntime run(settings, 0.25f);
		require(run.injectFault("loop", "coolant-leak", {}) && run.advanceTo(3), "dry leak failed");
		require(run.snapshot().coolantLoops[0].coolantKg == 0
			&& run.snapshot().coolantLoops[0].flowKgPerS == 0
			&& run.snapshot().atmospheres[0].spilledCoolantKg == 100, "dry loop invented coolant or flow");
	}
}
inline void repairLifecycle()
{
	station::ScopedStationStepRatioForTesting clock(std::nullopt);
	auto s = repairStation();
	station::StationSystemsRuntime run(s, 0.25f);
	require(run.injectFault("loop", "coolant-leak", {station::FaultCause::Kind::AgentAction, "agent:1"})
		&& run.advanceTo(1), "leak setup failed");
	require(run.snapshot().coolantLoops[0].availabilityFactor == 0.6, "leak cap missing before repair");
	std::string jobId;
	require(run.repairFault("loop", "coolant-leak", &jobId), "valid repair refused");
	require(jobId == "repair-1", "repair job id not as expected");
	auto const pending = run.snapshot();
	auto const* job = findJob(pending, "repair-1");
	require(job && job->kind == station::StationMaintenanceKind::Repair
		&& job->faultModeId == "coolant-leak" && job->installationId == "loop"
		&& job->phase == station::StationMaintenancePhase::Blocked
		&& job->requirements.size() == 1 && job->requirements[0].storeId == "parts"
		&& job->requirements[0].quantity == 2 && job->requirements[0].consumed
		&& !job->requirements[0].reserved,
		"pending repair missing from the maintenance picture with its requirement detail");
	require(run.advanceTo(3), "repair start failed audit");
	auto const mid = run.snapshot();
	job = findJob(mid, "repair-1");
	require(job && job->phase == station::StationMaintenancePhase::Active && job->isolated
		&& job->requirements[0].reserved && job->progressSeconds > 1.5 && job->remainingSeconds > 6.5,
		"repair did not start, reserve and progress");
	require(mid.coolantLoops[0].availabilityFactor == 0 && mid.coolantLoops[0].flowKgPerS == 0,
		"repair did not isolate the installation at zero availability");
	require(findStoreSnap(mid, "parts")->currentQualified == 10,
		"reserved parts left the store before completion");
	// The repair started at the command boundary (t=1.0) and owes its
	// full 10 s: still isolated just before the completion instant.
	require(run.advanceTo(10.5), "mid-repair drive failed");
	require(run.snapshot().coolantLoops[0].availabilityFactor == 0
		&& findJob(run.snapshot(), "repair-1")->phase == station::StationMaintenancePhase::Active,
		"repair stopped isolating before its full duration");
	require(run.advanceTo(11.5), "repair completion failed audit");
	auto const done = run.snapshot();
	job = findJob(done, "repair-1");
	require(job && job->phase == station::StationMaintenancePhase::Completed && job->exhausted
		&& job->occurrencesCompleted == 1 && !job->isolated
		&& job->requirements[0].lastConsumed == 2 && job->requirements[0].consumedTotal == 2,
		"repair did not complete as a full one-off");
	require(done.coolantLoops[0].activeFaults.empty()
		&& done.coolantLoops[0].availabilityFactor == 1
		&& done.coolantLoops[0].coolantKg == 60,
		"completion did not clear the fault cap, or it replenished coolant");
	require(findStoreSnap(done, "parts")->currentQualified == 8
		&& findStoreSnap(done, "waste")->currentQualified == 2
		&& findStoreSnap(done, "waste")->currentUnqualified == 0,
		"consumed parts did not land in waste as qualified material");
	require(countCategory(run, "repair") == 2 && sawMessage(run, "'repair-1'")
		&& sawMessage(run, "installation isolated") && sawMessage(run, "booked into waste store 'waste'"),
		"repair start/complete events missing");
	require(countCategory(run, "fault") == 2 && sawMessage(run, "cleared by repair 'repair-1'"),
		"fault clear event missing");
	std::string diagnostic;
	require(!run.repairFault("loop", "coolant-leak", nullptr, &diagnostic) && !diagnostic.empty(),
		"repair of a cleared fault accepted");
	// Re-damage after repair is a fresh instance with a fresh repair.
	require(run.injectFault("loop", "coolant-leak", {station::FaultCause::Kind::ScenarioEvent, "strike"})
		&& run.advanceTo(12), "re-injection failed");
	require(run.repairFault("loop", "coolant-leak", &jobId, &diagnostic) && jobId == "repair-2",
		"second repair after re-damage refused or misnumbered");
	require(run.advanceTo(22.5), "second repair failed its drive");
	auto const second = run.snapshot();
	require(second.coolantLoops[0].activeFaults.empty()
		&& findJob(second, "repair-2")->phase == station::StationMaintenancePhase::Completed
		&& findStoreSnap(second, "waste")->currentQualified == 4,
		"second repair did not clear and spend again");
}
inline void repairInterruption()
{
	station::ScopedStationStepRatioForTesting clock(std::nullopt);
	// A continuous drain from the parts store breaks the repair's
	// reservation mid-job: progress is discarded, nothing is spent, and
	// after a refill the job reruns the full duration from the restart -
	// the #414 interruption contract on the repair starter.
	auto s = repairStation();
	station::ResourceStoreSettings bilge;
	bilge.id = "bilge"; bilge.unit = "kg"; bilge.capacity = 1000; bilge.startingQualified = 0;
	s.stores.push_back(bilge);
	station::ResourceTransferSettings drain;
	drain.id = "drain"; drain.sourceId = "parts"; drain.destinationId = "bilge";
	drain.unit = "kg"; drain.ratePerSecond = 1; drain.priority = 0;
	s.transfers = {drain};
	station::ResourceDeliverySettings refill;
	refill.id = "refill"; refill.storeId = "parts"; refill.unit = "kg";
	refill.quantity = 20; refill.firstOccurrenceSeconds = 9; refill.recurrenceSeconds = 0;
	s.deliveries = {refill};
	station::StationSystemsRuntime run(s, 0.25f);
	require(run.injectFault("loop", "coolant-leak", {}) && run.advanceTo(1), "setup failed");
	require(run.repairFault("loop", "coolant-leak"), "repair with drawable parts refused");
	require(run.advanceTo(3) && findJob(run.snapshot(), "repair-1")->phase
		== station::StationMaintenancePhase::Active, "repair did not start");
	require(run.advanceTo(8.5), "drain drive failed");
	auto const cut = run.snapshot();
	auto const* job = findJob(cut, "repair-1");
	require(job && job->phase == station::StationMaintenancePhase::Blocked && job->progressSeconds == 0,
		"the lost reservation did not interrupt and discard progress");
	require(findStoreSnap(cut, "waste")->currentQualified == 0
		&& !cut.coolantLoops[0].activeFaults.empty(),
		"the interrupted repair spent material or cleared the fault anyway");
	require(sawMessage(run, "interrupted at") && sawMessage(run, "progress discarded"),
		"the interruption was not announced with its discarded progress");
	require(run.advanceTo(10), "refill drive failed");
	auto const restarted = run.snapshot();
	job = findJob(restarted, "repair-1");
	require(job && job->phase == station::StationMaintenancePhase::Active
		&& job->occurrencesStarted == 2 && job->progressSeconds < 2,
		"the repair did not rerun afresh after the refill");
	require(run.advanceTo(19.5), "rerun completion failed");
	auto const done = run.snapshot();
	job = findJob(done, "repair-1");
	require(job && job->phase == station::StationMaintenancePhase::Completed
		&& job->occurrencesCompleted == 1 && job->occurrencesMissed == 0,
		"the rerun did not complete exactly once");
	require(done.coolantLoops[0].activeFaults.empty() && findStoreSnap(done, "waste")->currentQualified == 2,
		"the rerun's spend or fault clear failed");
}
}
