#pragma once

#include "StationScenarioRig.h"
#include <set>

namespace smoke::station
{
// Verify overlapping informational occurrences through the published ledger and
// stream only, including the interval between the older and newer expiry.
inline void checkNotificationOccurrences(::station::StationSystemsRuntime& run, std::string const& metric)
{
	require(run.advanceTo(4), "notification transitions failed");
	std::vector<::station::StationAlertSnapshot> fired;
	for (auto const& alert : run.snapshot().activeAlerts)
		if (alert.metric == metric) fired.push_back(alert);
	require(fired.size() == 4, "not every transition fired an Alert");
	for (unsigned i = 0; i < fired.size(); ++i)
		require(fired[i].firedAtSeconds == i, "notification fire time wrong");
	for (unsigned second = 59; second <= 65; ++second)
	{
		require(run.advanceTo(second), "notification expiry drive failed");
		auto const snap = run.snapshot();
		for (auto const& occurrence : fired)
		{
			auto matches = [&](auto const& a) {
				return a.metric == metric && a.firedAtSeconds == occurrence.firedAtSeconds;
			};
			bool const expired = second >= occurrence.firedAtSeconds + 60;
			require(std::count_if(snap.activeAlerts.begin(), snap.activeAlerts.end(), matches) == !expired,
				"notification occurrence removed prematurely or retained after expiry");
			require(std::count_if(snap.closedAlerts.begin(), snap.closedAlerts.end(), matches) == expired,
				"notification occurrence missing closeout or cleared twice");
			if (expired)
			{
				auto const closed = std::find_if(snap.closedAlerts.begin(), snap.closedAlerts.end(), matches);
				require(closed->clearedAtSeconds == occurrence.firedAtSeconds + 60
					&& closed->clearReason == ::station::AlertClearReason::AutoClear
					&& closed->monitorId == occurrence.monitorId && closed->detail == occurrence.detail,
					"notification closeout time or identity wrong");
			}
		}
	}
	std::set<std::string> identities;
	for (auto const& occurrence : fired)
	{
		require(identities.insert(occurrence.monitorId).second, "notification occurrence identity reused");
		for (auto const* transition : {"fired", "cleared"})
			require(std::count_if(run.events().begin(), run.events().end(), [&](auto const& e) {
				return e.category == "alert" && e.message.find(std::string("Alert ") + transition
					+ " '" + occurrence.monitorId + "'") != std::string::npos;
			}) == 1, "notification occurrence event missing or duplicated");
	}
}
}
