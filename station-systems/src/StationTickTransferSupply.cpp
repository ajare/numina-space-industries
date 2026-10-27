#include "StationSystemsImpl.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace station
{
	namespace
	{
		// #882: how long a transfer Alert stays on the ledger before it clears
		// itself. A transfer is informational - the switch has already moved,
		// and nothing waits on the Alert - so it exists to be read and then to
		// retire on a fixed short window rather than to be acknowledged or to
		// latch beside a fault.
		constexpr double kSupplyTransferAlertSeconds{60.0};
		// The alert type spec §5.2/§8 names. Every fire and clear resolves
		// through the callback registry under this key (ADR-0031), so the
		// transfer Alert is announced the same way every other Alert is and
		// needs no authored Monitor.
		constexpr char const* kSupplyTransferAlertType{"supply-transfer"};
		// A shortfall of roundoff size is not a shortfall. The switch only
		// moves on a shortfall a meter could see, so a primary that can carry
		// its load to within audit noise never trips the switch.
		constexpr double kSupplyTransferEpsilonKW{1e-9};

		// Informational Alerts are occurrences, unlike a Monitor's single
		// active condition. Include the committed transition instant so repeated
		// same-direction transitions expire and close independently (#940).
		// Round-trip precision preserves distinct sub-second instants and keeps
		// identities deterministic across replay without a mutable counter.
		std::string notificationOccurrenceSuffix(double instant)
		{
			return std::format(":at:{:.17g}", instant);
		}

		// Direction identifies the switch transition; the occurrence suffix
		// below distinguishes repeated transitions within its Alert lifetime.
		std::string supplyTransferAlertId(bool toBackup, std::string const& type,
			std::string const& id)
		{
			return std::string{kSupplyTransferAlertType}
				+ (toBackup ? ":to-backup:" : ":to-primary:") + type + ':' + id;
		}
	}

	bool StationSystemsRuntime::Impl::Tick::transferSupply()
	{
		// Resolve against the authoritative allocation, not an independently
		// reused gross bus budget (#907). Trials start after generation and
		// roll back all runtime bookings; only the chosen switch positions
		// survive. This keeps protected admission, shared feeders, batteries,
		// material feasibility and cable losses identical to the real tick.
		using Key = std::pair<std::string, std::string>;
		struct Switch { Key key; std::string primary, backup; double dwell; };
		std::vector<Switch> switches;
		auto add = [&](char const* type, auto const& entry) {
			if constexpr (requires { entry.authored.sharedPowerFeed; })
				if (entry.authored.sharedPowerFeed) return;
			if (entry.authored.supplyMode == StationSupplyMode::Transfer
				&& entry.authored.supplyBusIds.size() == 2)
				switches.push_back({{type, entry.authored.id}, entry.authored.supplyBusIds[0],
					entry.authored.supplyBusIds[1], entry.authored.transferReturnDwellSeconds});
		};
		auto visit = [&](char const* type, auto const& entries) {
			for (auto const& entry : entries) add(type, entry);
		};
		// Equipment uses the allocator's stable priority order, not declaration
		// order. Other subsystems retain their authoritative phase order.
		for (auto const index : runtime.loadOrder) add("Equipment", runtime.loads[index]);
		visit("Generator", runtime.generators);
		visit("Coolant loop", runtime.coolantLoops);
		visit("Chiller", runtime.chillers);
		visit("Water treatment", runtime.waterTreatments);
		visit("Electrolyzer", runtime.electrolyzers);
		visit("CO2 capture", runtime.co2Captures);
		visit("Sabatier", runtime.sabatierReactors);
		visit("Methane processor", runtime.methaneProcessors);
		visit("Microbial processor", runtime.microbialProcessors);
		visit("Lighting fixture", runtime.lightingFixtures);
		visit("Crops", runtime.cropPopulations);
		visit("Algae", runtime.algaeCultures);
		if (switches.empty()) return true;

		// #908: both values are at the consumer (after cable losses).
		// Gross cable-input requests belong only to bus dispatch; using them
		// here would make switching depend on the currently serving cable.
		struct Service { double demand{}, supplied{}; };
		std::map<Key, Service> service;
		auto trial = [&](Switch const* returning) {
			auto const restore = runtime.ledgerCheckpoint();
			auto const savedPrediction = runtime.prediction;
			auto preview = *this;
			if (returning) preview.consumerBuses[returning->key] = returning->primary;
			auto const valid = preview.protection() && preview.thermal()
				&& preview.processing() && preview.biology() && preview.atmosphere()
				&& preview.wear() && preview.auditPower();
			service.clear();
			if (valid)
				for (auto const& item : switches)
					service[item.key] = {preview.dispatchRequests[item.key], preview.consumerSupplied[item.key]};
			restore();
			runtime.prediction = savedPrediction;
			return valid;
		};
		auto capable = [](Service const& value) {
			return value.supplied + kSupplyTransferEpsilonKW >= value.demand;
		};
		auto const switchInstant = elapsed - seconds;
		auto move = [&](Switch const& item, bool backup, Service const& value) {
			auto& state = runtime.supplyTransfers[item.key];
			state.onBackup = backup;
			state.primaryCapableSince.reset();
			state.eventFromBusId = backup ? item.primary : item.backup;
			state.eventToBusId = backup ? item.backup : item.primary;
			state.eventAtSimSeconds = switchInstant;
			state.eventDemandKW = value.demand;
			state.eventPrimaryAvailableKW = value.supplied;
			consumerBuses[item.key] = state.eventToBusId;
		};
		// Remove one short primary load at a time, lowest priority first,
		// then reallocate. Released protected claims may let another load
		// stay whole; never unnecessarily transfer every partial grant.
		std::vector<Key> moved;
		auto failover = [&]() {
			for (;;)
			{
				if (!trial(nullptr)) return false;
				auto const shortLoad = std::find_if(switches.rbegin(), switches.rend(), [&](auto const& item) {
					return consumerBuses.at(item.key) == item.primary && !capable(service.at(item.key));
				});
				if (shortLoad == switches.rend()) return true;
				move(*shortLoad, true, service.at(shortLoad->key));
				moved.push_back(shortLoad->key);
			}
		};
		if (!failover()) return false;
		// A returning load must fit alongside current competitors. Earlier
		// returns become competitors for later trials, so simultaneous dwell
		// expiry cannot return several loads against the same spare watts.
		for (auto const& item : switches)
		{
			if (consumerBuses.at(item.key) != item.backup
				|| std::find(moved.begin(), moved.end(), item.key) != moved.end()) continue;
			if (!trial(&item)) return false;
			auto const value = service.at(item.key);
			auto& state = runtime.supplyTransfers[item.key];
			if (!capable(value)) { state.primaryCapableSince.reset(); continue; }
			if (!state.primaryCapableSince) state.primaryCapableSince = switchInstant;
			if (*state.primaryCapableSince + item.dwell > switchInstant + kSupplyTransferEpsilonKW) continue;
			move(item, false, value);
			// A higher-priority return can displace a lower-priority incumbent.
			// Move that incumbent whole in this interval, not one tick later.
			if (!failover()) return false;
		}
		return true;
	}

	bool StationSystemsRuntime::Impl::Tick::standbySupply()
	{
		if (runtime.standbyPairings.empty()) return true;
		auto const instant = elapsed - seconds;
		bool changed = false;
		// Probe the actual allocator with the current standby positions. All
		// energy, inventory and telemetry bookings are rolled back before use.
		auto trial = [&]() {
			auto const restore = runtime.ledgerCheckpoint();
			auto const savedPrediction = runtime.prediction;
			auto preview = *this;
			auto const valid = preview.protection() && preview.thermal()
				&& preview.processing() && preview.biology() && preview.atmosphere()
				&& preview.wear() && preview.auditPower();
			std::vector<StationLightingFixtureSnapshot> result;
			if (valid) for (auto const& fixture : runtime.lightingFixtures) result.push_back(fixture.output);
			restore();
			runtime.prediction = savedPrediction;
			return std::pair{valid, result};
		};
		for (size_t i = 0; i < runtime.standbyPairings.size(); ++i)
		{
			auto const [valid, outputs] = trial();
			if (!valid) return false;
			auto const& authored = runtime.settings.standbyPairings[i];
			auto const primary = std::find_if(runtime.lightingFixtures.begin(), runtime.lightingFixtures.end(),
				[&](auto const& f) { return f.authored.id == authored.primaryInstallationId; });
			auto const& supply = outputs[static_cast<size_t>(primary - runtime.lightingFixtures.begin())];
			auto const scheduled = evaluateStationProfile(primary->authored.operatingProfile, profileElapsed) > 0;
			auto& state = runtime.standbyPairings[i];
			auto& out = state.output;
			if (out.state == StationStandbyState::Idle)
			{
				if (scheduled && !primary->failure && primary->authored.fixtureType.ratedKW > 0
					&& supply.suppliedElectricalKW <= 1e-9)
				{
					out.state = StationStandbyState::Active;
					changed = true;
					out.activatedAtSimSeconds = instant;
					state.released = false;
					runtime.standbyEvents.push_back({authored.standbyInstallationId, true, instant});
				}
				continue;
			}
			// Partial recovery is not capability: a marginal primary restarts
			// the dwell, although only complete power loss triggers activation.
			if (primary->failure || (scheduled && supply.suppliedElectricalKW <= 1e-9)
				|| supply.suppliedElectricalKW + 1e-9 < supply.requestedElectricalKW)
			{
				out.state = StationStandbyState::Active;
				out.primaryCapableSince.reset();
				continue;
			}
			if (!out.primaryCapableSince) out.primaryCapableSince = instant;
			out.state = StationStandbyState::Returning;
			if (*out.primaryCapableSince + authored.returnDwellSeconds > instant + 1e-9) continue;
			out.state = StationStandbyState::Idle;
			out.returnedAtSimSeconds = instant;
			out.primaryCapableSince.reset();
			state.released = true;
			changed = true;
			runtime.standbyEvents.push_back({authored.standbyInstallationId, false, instant});
		}
		// Newly requested standby demand must use ordinary Transfer semantics
		// in this interval too, not wait another Station step to switch buses.
		return !changed || transferSupply();
	}

	void StationSystemsRuntime::Impl::reconcileStandbyAlerts()
	{
		AlertSink sink{activeAlerts, closedAlerts, events, clock.steps()};
		for (auto const& event : standbyEvents)
		{
			StationAlertSnapshot alert;
			alert.metric = "standby-pairing";
			alert.monitorId = alert.metric + (event.activated ? ":activated:" : ":returned:") + event.id
				+ notificationOccurrenceSuffix(event.at);
			alert.subjectType = "Lighting fixture";
			alert.subjectId = event.id;
			alert.severity = AlertSeverity::Advisory;
			alert.provenance = MonitorProvenance::Custom;
			alert.firedAtSeconds = event.at;
			alert.detail = event.activated ? "Standby activated" : "Standby returned to operating profile";
			stationAlertCallbacksFor(alert.metric).onFire(sink, alert);
		}
		standbyEvents.clear();
		std::vector<std::string> expired;
		for (auto const& alert : activeAlerts)
			if (alert.metric == "standby-pairing" && clock.elapsedSeconds() >= alert.firedAtSeconds + 60)
				expired.push_back(alert.monitorId);
		for (auto const& id : expired) clearAlert(id, AlertClearReason::AutoClear);
	}

	void StationSystemsRuntime::Impl::reconcileTransferAlerts()
	{
		// #882: the Advisory announcement for every transfer the committed
		// tick recorded. This runs outside the ledger beside the Monitor
		// evaluation on purpose. The switch actuated inside the tick; only its
		// announcement waits for the commit. A tick that failed and rolled back
		// took its transfer event with it, so no Alert is ever raised for a
		// move that never happened, and the Alert ledger never needs to be
		// part of the rollback state.
		auto const now = clock.elapsedSeconds();
		AlertSink sink{activeAlerts, closedAlerts, events, clock.steps()};
		for (auto& [key, state] : supplyTransfers)
		{
			if (state.eventAtSimSeconds < 0.0
				|| state.alertRaisedForEventSeconds == state.eventAtSimSeconds)
				continue;
			StationAlertSnapshot alert;
			alert.monitorId = supplyTransferAlertId(state.onBackup, key.first, key.second)
				+ notificationOccurrenceSuffix(state.eventAtSimSeconds);
			alert.subjectType = key.first;
			alert.subjectId = key.second;
			alert.metric = kSupplyTransferAlertType;
			// The Alert reads as the measurement that moved the switch: what
			// the load wanted, against what the primary could put on the table.
			alert.currentValue = state.eventDemandKW;
			alert.threshold = state.eventPrimaryAvailableKW;
			alert.severity = AlertSeverity::Advisory;
			alert.provenance = MonitorProvenance::Custom;
			alert.firedAtSeconds = state.eventAtSimSeconds;
			alert.detail = state.eventFromBusId + " -> " + state.eventToBusId;
			state.alertRaisedForEventSeconds = state.eventAtSimSeconds;
			stationAlertCallbacksFor(kSupplyTransferAlertType).onFire(sink, alert);
		}
		// Advisory, not latched: each transfer Alert retires on its own fixed
		// short window whether or not the load has moved again since.
		std::vector<std::string> expired;
		for (auto const& alert : activeAlerts)
			if (alert.metric == kSupplyTransferAlertType
				&& now >= alert.firedAtSeconds + kSupplyTransferAlertSeconds)
				expired.push_back(alert.monitorId);
		for (auto const& monitorId : expired) clearAlert(monitorId, AlertClearReason::AutoClear);
	}
}
