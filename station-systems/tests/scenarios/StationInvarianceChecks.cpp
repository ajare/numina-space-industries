#include "Smoke.h"
#include "station/StationObservableInvariance.h"
#include "station/StationSystemsRuntime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	constexpr float quantum = 1.0f / 60.0f;
	// Bounds, production default, an odd/non-divisor step, and a coarse interior sample.
	constexpr std::array<uint32_t, 5> representativeRatios{1, 6, 7, 30, 60};
	void require(bool value, char const* message) { if (!value) throw std::runtime_error(message); }

	::station::StationSystemsSettings scenario(uint32_t ratio, bool saturated)
	{
		::station::StationSystemsSettings settings;
		settings.enabled = true;
		settings.systemsStepWorldTicks = ratio;
		settings.stores = {{"source", "kg", 20000, 10000, 0},
			{"sink", "kg", saturated ? 1.0 : 20000.0, 0, 0}};
		::station::ResourceTransferSettings transfer;
		transfer.id = "transfer"; transfer.sourceId = "source"; transfer.destinationId = "sink";
		transfer.unit = "kg"; transfer.ratePerSecond = 1;
		settings.transfers = {transfer};
		settings.deliveries = {{"delivery", "source", "kg", 1, 0.025}};
		return settings;
	}

	::station::StationSystemsSnapshot run(uint32_t ratio, double durationSeconds, bool saturated = false)
	{
		::station::StationSystemsRuntime runtime(scenario(ratio, saturated), quantum);
		require(runtime.snapshot().systemsStepWorldTicks == ratio
			&& runtime.snapshot().systemsTimestepSeconds == ratio * static_cast<double>(quantum),
			"Invariance test did not execute its independently authored timestep");
		require(runtime.advanceTo(durationSeconds), "Cross-step scenario did not advance");
		return runtime.snapshot();
	}

	void compare(::station::StationSystemsSnapshot const& reference,
		::station::StationSystemsSnapshot const& candidate)
	{
		require(reference.healthy && candidate.healthy, "Cross-step runtime failed");
		require(reference.enabled == candidate.enabled
			&& reference.balanceTolerance == candidate.balanceTolerance,
			"Clock-independent published setting changed with timestep");
		require(reference.elapsedSeconds == candidate.elapsedSeconds,
			"Cross-step comparison used different simulated instants");
		auto const close = [](double a, double b) {
			return std::isfinite(a) && std::isfinite(b)
				&& std::abs(a - b) <= 1e-12 * std::max({1.0, std::abs(a), std::abs(b)});
		};
		require(reference.stores.size() == candidate.stores.size(), "Store catalogue changed with timestep");
		for (size_t i = 0; i < reference.stores.size(); ++i)
		{
			auto const& a = reference.stores[i]; auto const& b = candidate.stores[i];
			require(a.id == b.id && close(a.currentQualified, b.currentQualified)
				&& close(a.currentUnqualified, b.currentUnqualified),
				"Physical store inventory changed with timestep");
		}
		// Transfer cumulativeMoved is an occurrence counter for Scheduled
		// transfers only. Continuous movement is proved by inventories and
		// actual cumulative connection accounts, not two always-zero fields.
		require(close(reference.deliveries[0].acceptedTotal, candidate.deliveries[0].acceptedTotal)
			&& reference.deliveries[0].occurrencesCompleted == candidate.deliveries[0].occurrencesCompleted,
			"Cumulative physical exchange or occurrence count changed with timestep");
		for (auto const& b : candidate.balances)
			require(close(b.residual, 0), "Cross-step material conservation failed");
		require(reference.connections.size() == candidate.connections.size(),
			"Connection catalogue changed with timestep");
		for (size_t i = 0; i < candidate.connections.size(); ++i)
		{
			auto const& a = reference.connections[i]; auto const& c = candidate.connections[i];
			require(a.key == c.key && close(a.inputTotal, c.inputTotal)
				&& close(a.deliveredTotal, c.deliveredTotal) && close(a.lostTotal, c.lostTotal),
				"Cumulative connection exchange changed with timestep");
			require(close(c.inputTotal, c.deliveredTotal + c.lostTotal),
				"Cross-step connection conservation failed");
		}
		// Per-step doses and step counters are intentionally not compared as
		// physical invariants. Their units depend on the effective timestep.
	}

	void compareRatio(uint32_t ratio, double seconds)
	{
		std::cout << "COMPARE station reference-ratio=1 candidate-ratio=" << ratio
			<< " target-seconds=" << seconds << '\n';
		for (bool saturated : {false, true})
		{
			auto const reference = run(1, seconds, saturated);
			auto const candidate = run(ratio, seconds, saturated);
			compare(reference, candidate);
			auto const expectedMoved = saturated ? 1.0 : seconds;
			require(std::abs(candidate.stores[1].currentQualified - expectedMoved) < 1e-9,
				"Cross-step exchange disagrees with the analytical rate/capacity result");
		}
	}

	void invarianceRegistry(smoke::Context const&)
	{
		std::set<std::string_view> names;
		for (auto const& entry : ::station::stationObservableInvariances())
		{
			require(!entry.name.empty() && !entry.basis.empty() && names.insert(entry.name).second,
				"Observable classification is missing a basis or has a duplicate name");
			require(entry.certifiedRatio <= ::station::CORE_MAX_SYSTEMS_STEP_WORLD_TICKS
				&& entry.verifiedThroughRatio <= ::station::CORE_MAX_SYSTEMS_STEP_WORLD_TICKS,
				"Observable evidence exceeds the supported timestep range");
			if (entry.classification == ::station::StationObservableInvarianceClass::Unverified)
				require(entry.certifiedRatio == 0 && entry.verifiedThroughRatio == 0 && entry.verifiedBy.empty(),
					"Unverified aggregate advertises a measured guarantee");
			else
				require(!entry.verifiedBy.empty(), "Observable claim does not name its actual verifier");
		}
		for (auto const& entry : ::station::stationObservableInvariances())
			if (entry.name.find("[]") != std::string_view::npos)
				require(entry.certifiedRatio == 0 && entry.verifiedThroughRatio == 60,
					"Fixture-scoped evidence was promoted to a global physics guarantee");
		require(names.size() == 70 && names.contains("variableTimestep") && names.contains("solarCycle") && names.contains("lightingFixtures") && names.contains("standbyPairings")
			&& names.contains("heatSources") && names.contains("atmospherePathways")
			&& names.contains("thermalLinks") && names.contains("heatEngines")
			&& names.contains("activeAlerts") && names.contains("closedAlerts")
			&& names.contains("atmospherePathways[].lastCommandSeconds")
			&& names.contains("atmospherePathways[].lastCommandId")
			&& names.contains("atmospherePathways[].openFraction") && names.contains("systemsStepWorldTicks")
			&& names.contains("systemsTimestepSeconds") && names.contains("stores[].currentQualified"),
			"Observable coverage omitted a published clock or verified physical field");
	}

	void invariance(smoke::Context const&)
	{
		// A CLI lane selects one candidate, never the reference. Scoped
		// ownership also prevents a comparison from changing later checks.
		auto const ratio = ::station::stationSystemsRuntimeStepRatioOverrideForTesting().value_or(6);
		::station::ScopedStationStepRatioForTesting explicitClock(std::nullopt);
		compareRatio(ratio, 60.0 * ratio * static_cast<double>(quantum));
	}

	void invarianceRepresentative(smoke::Context const&)
	{
		::station::ScopedStationStepRatioForTesting explicitClock(std::nullopt);
		for (uint32_t ratio : representativeRatios)
			// Sixty candidate steps give an exact common comparison boundary,
			// including for the non-divisor ratio 7.
			compareRatio(ratio, 60.0 * ratio * static_cast<double>(quantum));
	}

	void invarianceProfiles(smoke::Context const&)
	{
		::station::ScopedStationStepRatioForTesting explicitClock(std::nullopt);
		for (bool nonOverlappingSupply : {false, true})
		for (uint32_t ratio : representativeRatios)
		{
			auto settings = scenario(ratio, false);
			settings.transfers.clear(); settings.deliveries.clear();
			settings.solarArrays = {{"sun", 10}};
			::station::EquipmentLoadSettings load;
			load.id = "load"; load.electricalDemandKW = 1;
			load.coolingDisposition = ::station::StationCoolingDisposition::Exported;
			load.thermalClass = "low";
			load.operatingProfile.periodSeconds = 1;
			load.operatingProfile.segments = {{0, 1}, {0.25, 0}, {0.75, 1}};
			if (nonOverlappingSupply)
			{
				settings.solarArrays[0].capacityKW = 1;
				settings.solarCycle = load.operatingProfile;
				for (auto& segment : settings.solarCycle->segments) segment.level = 1 - segment.level;
			}
			load.materialExchanges = {{"source", 1, true}, {"sink", 1, false}};
			settings.equipmentLoads = {load};
			auto const seconds = ratio * 60.0 * static_cast<double>(quantum);
			::station::StationSystemsRuntime candidate(settings, quantum);
			settings.systemsStepWorldTicks = 1;
			::station::StationSystemsRuntime reference(settings, quantum);
			require(candidate.snapshot().systemsStepWorldTicks == ratio
				&& reference.snapshot().systemsStepWorldTicks == 1, "Profile comparison ratios collapsed");
			require(candidate.advanceTo(seconds) && reference.advanceTo(seconds), "Profile integration failed");
			auto const a = reference.snapshot(), b = candidate.snapshot();
			auto const cycles = std::floor(seconds);
			auto const tail = seconds - cycles;
			auto const expected = nonOverlappingSupply ? 0.0
				: cycles * 0.5 + std::min(tail, 0.25) + std::max(0.0, tail - 0.75);
			std::cout << "PROFILE ratio=" << ratio << " moved=" << b.stores[1].currentQualified
				<< " analytical=" << expected << '\n';
			require(a.elapsedSeconds == b.elapsedSeconds
				&& std::abs(a.stores[1].currentQualified - expected) < 1e-9
				&& std::abs(b.stores[1].currentQualified - expected) < 1e-9,
				"Profile-weighted exchange depends on the integration timestep");
		}
	}

	void invarianceGeneratorTiming(smoke::Context const&)
	{
		::station::ScopedStationStepRatioForTesting explicitClock(std::nullopt);
		// Constant qualification/supply isolates startup and a linear ramp:
		// output(t) = min(8, 2 * max(0, t - 0.2)) kW. Its integral is
		// analytical; the ratio-1 numerical result is not ground truth.
		double worstError = 0;
		for (unsigned nominalWorldTicks : {60u, 360u})
		for (bool delayedSolar : {false, true})
		for (uint32_t ratio : representativeRatios)
		{
			auto const seconds = std::ceil(static_cast<double>(nominalWorldTicks) / ratio)
				* ratio * static_cast<double>(quantum);
			auto const startSeconds = delayedSolar ? 0.5 : 0.0;
			auto const integral = [&](double t) {
				auto const active = std::max(0.0, t - startSeconds - 0.2);
				auto const ramp = std::min(4.0, active);
				return ramp * ramp + 8 * std::max(0.0, active - ramp);
			};
			auto const expectedKJ = integral(seconds);
			std::ostringstream assertions;
			assertions << std::setprecision(17) << "\n        if station_step * fixed_seconds >= " << seconds
				<< " then assert(math.abs(self.generators[1].electricalKJ - " << expectedKJ
				<< ") < 1e-8, 'Lua generator work disagrees with physical-time integral') end\n";
			auto const stationSource1 = ([injected = assertions.str()] {
				auto source = ::station::bundledStationSystemsSource();
				auto const at = source.find("        self.ticks = self.ticks + 1");
				require(at != std::string::npos, "Generator Lua regression assertion point missing");
				source.insert(at, injected);
				return source;
			})();
			::station::StationSystemsSettings settings;
			settings.enabled = true; settings.systemsStepWorldTicks = ratio;
			settings.stores = {{"fuel", "kWh", 100, 100}};
			settings.solarArrays = {{"sun", 20}};
			if (delayedSolar)
			{
				settings.solarCycle.emplace().periodSeconds = 100;
				settings.solarCycle->segments = {{0, 0}, {0.5, 1}};
			}
			::station::ThermalReservoirSettings sink;
			sink.id = "sink"; sink.thermalClass = "warm"; sink.heatCapacityKJPerK = 1e6;
			sink.startingTemperatureC = 20; sink.minTemperatureC = 0; sink.maxTemperatureC = 100;
			settings.thermalReservoirs = {sink};
			::station::GeneratorSettings generator;
			generator.id = "generator"; generator.fuelStoreId = "fuel";
			generator.heatSinkId = "sink"; generator.thermalClass = "warm";
			generator.ratedOutputKW = 10; generator.commandedOutputKW = 8;
			generator.efficiency = 0.5; generator.maxRampKWPerSecond = 2;
			generator.startupPowerKW = 1; generator.startupSeconds = 0.2;
			generator.maxSinkTemperatureC = 90;
			settings.generators = {generator};
			::station::StationSystemsRuntime runtime(settings, quantum, stationSource1);
			require(runtime.snapshot().systemsStepWorldTicks == ratio, "Generator proof ratio was overridden");
			if (!runtime.advanceTo(seconds))
				throw std::runtime_error("Generator timing proof failed its runtime audits: "
					+ (runtime.diagnostics().empty() ? std::string("no diagnostic") : runtime.diagnostics().front().message));
			auto const snapshot = runtime.snapshot();
			require(snapshot.elapsedSeconds == seconds, "Generator proof compared different physical instants");
			auto const& live = snapshot.power.generators[0];
			std::cout << "GENERATOR ratio=" << ratio << " delayed-solar=" << delayedSolar
				<< " electrical-kJ=" << live.electricalKJ
				<< " analytical-kJ=" << expectedKJ << '\n';
			require(std::abs(live.startupHeatKJ - 0.2) < 1e-10, "Generator crank energy is not conserved");
			auto const dt = snapshot.systemsTimestepSeconds;
			auto const work = expectedKJ - integral(seconds - dt);
			auto const crank = [&](double t) { return std::clamp(t - startSeconds, 0.0, 0.2); };
			auto const crankKW = (crank(seconds) - crank(seconds - dt)) / dt;
			auto const solarKW = 20 * (std::max(0.0, seconds - startSeconds)
				- std::max(0.0, seconds - dt - startSeconds)) / dt;
			require(std::abs(live.outputKW - std::min(8.0, 2 * (seconds - startSeconds - 0.2))) < 1e-10,
				"Generator endpoint power is not the physical ramp state");
			require(std::abs(live.averageOutputKW * dt - work) < 1e-8
				&& std::abs(live.fuelDrawKWh * 3600 * generator.efficiency - work) < 1e-8
				&& std::abs(live.startupDrawKW - crankKW) < 1e-8,
				"Generator step-mean telemetry disagrees with integrated work");
			require(std::abs(snapshot.power.poolGenerationKW - solarKW - live.averageOutputKW) < 1e-8
				&& std::abs(snapshot.power.surplusKW - solarKW - live.averageOutputKW + crankKW) < 1e-8,
				"Split generator interval published inconsistent mean pool power");
			for (auto const& connection : snapshot.connections)
				if (connection.key.ownerType == "Generator" && connection.key.relation == "Generation")
					require(std::abs(connection.lastInputRate - live.averageOutputKW) < 1e-8
						&& std::abs(connection.deliveredTotal - expectedKJ) < 1e-8,
						"Generator connection rate/total disagrees with integrated work");
			worstError = std::max(worstError, std::abs(live.electricalKJ - expectedKJ));
		}
		require(worstError < 1e-8, "Generator startup/ramp work depends on Station step latency instead of physical time");
	}

	void invarianceGeneratorFunding(smoke::Context const&)
	{
		::station::ScopedStationStepRatioForTesting explicitClock(std::nullopt);
		for (double crankSeconds : {1e-13, 0.001, 1.0})
		for (uint32_t ratio : representativeRatios)
		{
			auto const seconds = std::ceil(60.0 / ratio) * ratio * static_cast<double>(quantum);
			auto const expectedCrankKJ = std::clamp(seconds - 0.01, 0.0, crankSeconds);
			auto const active = std::max(0.0, seconds - 0.01 - crankSeconds);
			auto const ramp = std::min(0.01, active);
			auto const expectedWork = 50 * ramp * ramp + std::max(0.0, active - ramp);
			std::ostringstream assertions;
			assertions << std::setprecision(17) << "\n        if station_step * fixed_seconds >= " << seconds
				<< " then assert(math.abs(self.generators[2].startupReceivedKJ - " << expectedCrankKJ
				<< ") < 1e-8 and math.abs(self.generators[2].electricalKJ - " << expectedWork
				<< ") < 1e-8, 'Lua generator-funded crank crossed its power threshold at the wrong time') end\n";
			auto const stationSource2 = ([injected = assertions.str()] {
				auto source = ::station::bundledStationSystemsSource();
				auto const at = source.find("        self.ticks = self.ticks + 1");
				require(at != std::string::npos, "Generator funding Lua assertion point missing");
				source.insert(at, injected); return source;
			})();
			::station::StationSystemsSettings settings;
			settings.enabled = true; settings.systemsStepWorldTicks = ratio;
			settings.stores = {{"fuel", "kWh", 100, 100}};
			::station::ThermalReservoirSettings sink;
			sink.id = "sink"; sink.thermalClass = "warm"; sink.heatCapacityKJPerK = 1e6;
			sink.startingTemperatureC = 20; sink.minTemperatureC = 0; sink.maxTemperatureC = 100;
			settings.thermalReservoirs = {sink};
			::station::GeneratorSettings source;
			source.id = "source"; source.fuelStoreId = "fuel"; source.heatSinkId = "sink";
			source.thermalClass = "warm"; source.ratedOutputKW = 2; source.commandedOutputKW = 2;
			source.efficiency = 0.5; source.maxRampKWPerSecond = 100; source.maxSinkTemperatureC = 90;
			auto crank = source; crank.id = "crank"; crank.ratedOutputKW = 1; crank.commandedOutputKW = 1;
			crank.startupPowerKW = 1; crank.startupSeconds = crankSeconds;
			settings.generators = {source, crank};
			::station::EquipmentLoadSettings load;
			load.id = "load"; load.electricalDemandKW = 1; load.thermalClass = "warm";
			settings.equipmentLoads = {load};
			::station::StationSystemsRuntime runtime(settings, quantum, stationSource2);
			require(runtime.snapshot().systemsStepWorldTicks == ratio, "Generator funding proof ratio was overridden");
			if (!runtime.advanceTo(seconds))
				throw std::runtime_error("Generator funding proof failed its runtime audits: "
					+ (runtime.diagnostics().empty() ? std::string("no diagnostic") : runtime.diagnostics().front().message));
			auto const snap = runtime.snapshot();
			// Source reaches the crank's 1 kW threshold at t=0.01 s.
			// A whole-interval average must not fund the crank before then.
			std::cout << "GENERATOR-FUNDING ratio=" << ratio << " crank-seconds=" << crankSeconds << " crank-kJ="
				<< snap.power.generators[1].startupReceivedKJ << " analytical=" << expectedCrankKJ << '\n';
			require(snap.elapsedSeconds == seconds
				&& std::abs(snap.power.generators[1].startupReceivedKJ - expectedCrankKJ) < 1e-8
				&& std::abs(snap.power.generators[1].electricalKJ - expectedWork) < 1e-8
				&& std::abs(snap.power.generators[0].electricalKJ - (2 * seconds - 0.02)) < 1e-8,
				"Generator-funded work does not follow its physical power threshold");
			auto const overlap = std::min(0.01, crankSeconds);
			auto const expectedLoadWork = seconds - 0.005 - overlap + 50 * overlap * overlap;
			size_t loadAccounts = 0;
			for (auto const& connection : snap.connections)
				if (connection.key.ownerType == "Equipment" && connection.key.ownerId == "load"
					&& connection.key.relation == "Electrical supply")
				{
					++loadAccounts;
					require(std::abs(connection.deliveredTotal - expectedLoadWork) < 1e-8,
						"Load service did not retain the source/crank temporal overlap");
				}
			require(loadAccounts == 1, "Generator funding proof has no authoritative load account");
		}
	}

	void invarianceProfileRollback(smoke::Context const&)
	{
		::station::ScopedStationStepRatioForTesting explicitClock(std::nullopt);
		// Isolate the authoritative host transition guard from the Lua
		// mirror's equivalent guard and its instruction budget.
		auto const stationSource3 = ([] {
			return "return function() return {advance=function(self, step) return step end} end";
		})();
		auto settings = scenario(60, false);
		settings.solarArrays = {{"sun", 10}};
		settings.solarCycle.emplace().periodSeconds = 1e-8;
		settings.solarCycle->segments = {{0, 1}, {5e-9, 0}};
		::station::StationSystemsRuntime runtime(settings, quantum, stationSource3);
		auto const before = runtime.snapshot();
		require(!runtime.advanceTo(1), "Unbounded profile transition count was accepted");
		auto after = runtime.snapshot();
		require(!after.healthy && !runtime.diagnostics().empty(), "Profile refusal has no failure diagnostic");
		after.healthy = before.healthy;
		require(after == before, "Profile transition refusal published a partial clock or physical state");
		// The Station sun is environmental forcing, independent of array capacity.

		settings.solarArrays[0].capacityKW = 0;
		::station::StationSystemsRuntime disabled(settings, quantum);
		require(!disabled.advanceTo(1),
			"Disabled array incorrectly disabled Station solar-cycle validation");
	}

	void invarianceLongRun(smoke::Context const&)
	{
		::station::ScopedStationStepRatioForTesting explicitClock(std::nullopt);
		auto const seconds = 3600.0 * 60 * static_cast<double>(quantum);
		for (uint32_t ratio : {6u, 30u, 60u}) compareRatio(ratio, seconds);
	}

	void invariancePerformance(smoke::Context const&)
	{
		::station::ScopedStationStepRatioForTesting explicitClock(std::nullopt);
		auto const measure = [](uint32_t ratio)
		{
			::station::StationSystemsSettings settings;
			settings.enabled = true;
			settings.systemsStepWorldTicks = ratio;
			::station::StationSystemsRuntime runtime(settings, quantum);
			require(runtime.snapshot().systemsStepWorldTicks == ratio, "Benchmark ratio was overridden");
			auto const started = std::chrono::steady_clock::now();
			require(runtime.advanceTo(600.0), "Performance scenario did not advance");
			return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
		};
		auto const fine = measure(1), coarse = measure(6);
		std::cout << "Station timestep performance: ratio-1=" << fine * 6
			<< "s/hour ratio-6=" << coarse * 6 << "s/hour speedup=" << fine / coarse << '\n';
		require(coarse <= fine, "Coarse Station step is slower per simulated second");
	}
}

void registerStationInvarianceChecks(std::vector<smoke::Check>& checks)
{
	checks.push_back({"invariance", invariance});
	checks.push_back({"invarianceRegistry", invarianceRegistry});
	checks.push_back({"invarianceRepresentative", invarianceRepresentative});
	checks.push_back({"invarianceProfiles", invarianceProfiles});
	checks.push_back({"invarianceProfileRollback", invarianceProfileRollback});
	checks.push_back({"invarianceGeneratorTiming", invarianceGeneratorTiming});
	checks.push_back({"invarianceGeneratorFunding", invarianceGeneratorFunding});
	checks.push_back({"invarianceLongRun", invarianceLongRun});
	checks.push_back({"invariancePerformance", invariancePerformance});
}
