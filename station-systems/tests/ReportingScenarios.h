#pragma once
#include "station/StationSystemsRuntime.h"
#include <cmath>
#include <stdexcept>

namespace station_test::ReportingScenarios
{
	inline void require(bool condition, char const* message) { if (!condition) throw std::runtime_error(message); }
	inline void close(double actual, double expected)
	{
		require(std::isfinite(actual) && std::abs(actual - expected) < 1e-8, "battery observation numerical mismatch");
	}
	inline ::station::StationSystemsSettings rig(bool charging)
	{
		::station::StationSystemsSettings s; s.enabled = true; s.observationIntervalSeconds = 0.1f;
		s.trendRetentionSeconds = 3600;
		// Deliberately reverse identity order; percentage means 2/10, not
		// the unweighted average of 100% and 0%.
		s.batteries = {{"z-small", 2, 2, 2, 4, 4}, {"a-large", 8, 4, 0, 4, 4}};
		if (charging) s.solarArrays = {{"sun", 2}};
		else
		{
			::station::EquipmentLoadSettings load;
			load.id = "load"; load.electricalDemandKW = 1; load.thermalClass = "low";
			load.coolingDisposition = ::station::StationCoolingDisposition::Exported;
			s.equipmentLoads = {load};
		}
		return s;
	}	inline void batteryObservations()
	{
		for (bool charging : {false, true})
		{
			::station::StationSystemsRuntime world(rig(charging), 1.0f / 60.0f);
			auto const catalogue = world.catalogue();
			require(catalogue.size() >= 4 && catalogue[1].id == "battery-energy" && catalogue[2].id == "battery-charge",
				"battery properties were not discovered before recording");
			require(world.advanceTo(1.0), "battery observation run failed");
			require(world.queryWork().queries == 0, "recording built unselected views");
			auto const before = world.snapshot();
			auto const revision = world.observationRevision();
			auto const energy = world.query("battery-energy");
			auto const charge = world.query("battery-charge");
			require(energy.series.size() == 3 && charge.series.size() == 1 && !energy.incomplete && !charge.incomplete,
				"complete batteries produced incomplete queries");
			require(energy.series[1].label == "a-large" && energy.series[2].label == "z-small"
				&& energy.series[1].stacked && !energy.series[0].stacked, "stack grouping/identity was not stable");
			require(energy.series[0].values.size() == 10 && energy.series[0].times == charge.series[0].times,
				"selecting a battery view lost previously recorded history");
			double stored = 0, capacity = 0;
			for (auto const& battery : before.power.batteries) { stored += battery.storedKWh; capacity += battery.nameplateKWh; }
			close(energy.series[0].values.back(), stored);
			close(charge.series[0].values.back(), stored / capacity * 100);
			require(charging ? stored > 2 : stored < 2, "fixture did not charge/discharge");
			require(charge.series[0].values.back() < 21, "percentages were summed or averaged without capacity weighting");
			auto checkStack = [&](::station::StationChartData const& e, ::station::StationChartData const& c) {
				for (size_t row = 0; row < e.series[0].times.size(); ++row)
				{
					close(e.series[0].values[row], e.series[1].values[row] + e.series[2].values[row]);
					close(e.series[1].lower[row], 0);
					close(e.series[2].lower[row], e.series[1].upper[row]);
					close(e.series[2].upper[row], e.series[0].values[row]);
					close(c.series[0].values[row], e.series[0].values[row] / 10 * 100);
					require(e.series[1].times == e.series[0].times && e.series[2].times == c.series[0].times,
						"stack/sum/ratio timelines differ");
				}
			};
			checkStack(energy, charge);
			require(world.observationRevision() == revision
				&& world.snapshot() == before,
				"battery selection mutated simulation or document state");
			// Fault injection at the public exported observation seam, retaining
			// the applied expected membership rather than rediscovering a subset.
			auto history = world.metrics();
			auto missing = history;
			std::erase_if(missing, [](auto const& h) { return h.descriptor.id == "battery/a-large/stored-energy"; });
			auto partial = ::station::queryStationChart(catalogue[1], missing);
			auto partialCharge = ::station::queryStationChart(catalogue[2], missing);
			require(partial.incomplete && partialCharge.incomplete && std::isnan(partial.series[0].values[0])
				&& std::isnan(partial.series[2].upper[0]) && std::isnan(partialCharge.series[0].values[0]),
				"missing expected contribution became zero or a plausible partial total/stack");
			for (auto& h : history) if (h.descriptor.id == "battery/a-large/capacity") h.samples[0].elapsedSeconds += 0.01;
			auto unaligned = ::station::queryStationChart(catalogue[2], history);
			require(unaligned.incomplete && std::isnan(unaligned.series[0].values[0])
				&& std::isfinite(unaligned.series[0].values[1]), "ratio joined incompatible timestamps or lost valid spans");
			for (auto& h : history) if (h.descriptor.role == ::station::StationMetricRole::Capacity)
				h.descriptor.unit = ::station::StationCanonicalUnit::Kilogram;
			auto const incompatible = ::station::queryStationChart(catalogue[2], history);
			require(incompatible.incomplete && std::isnan(incompatible.series[0].values[1]),
				"ratio accepted incompatible physical units");
			require(world.advanceTo(31.0), "battery coarsening run failed");
			auto const coarseEnergy = world.query("battery-energy");
			auto const coarseCharge = world.query("battery-charge");
			require(coarseEnergy.truncated && coarseCharge.truncated && coarseEnergy.series[0].values.size() <= 256
				&& coarseEnergy.sampleIntervalSeconds > energy.sampleIntervalSeconds
				&& coarseEnergy.series[0].times == coarseCharge.series[0].times, "coarsened battery history is unbounded or unaligned");
			checkStack(coarseEnergy, coarseCharge);
			// Real property removal: the catalogue follows the newly applied run.
			auto empty = rig(charging); empty.batteries.clear();
			::station::StationSystemsRuntime replacement(empty, 1.0f / 60.0f);
			auto const remaining = replacement.catalogue();
			require(std::none_of(remaining.begin(), remaining.end(), [](auto const& c) { return c.group == "Batteries"; })
				&& replacement.query("battery-charge").series.empty(), "absent batteries manufactured a percentage view");
		}
		auto s = rig(true); s.batteries = {{"zero", 0, 0, 0}};
		::station::StationSystemsRuntime zero(s, 1.0f / 60.0f);
		require(zero.advanceTo(0.1), "zero-capacity run failed");
		auto const energy = zero.query("battery-energy");
		auto const charge = zero.query("battery-charge");
		close(energy.series[0].values[0], 0);
		require(charge.incomplete && std::isnan(charge.series[0].values[0]), "zero denominator generated a finite percentage or infinity");
		// Invalid observations must survive coarsening as gaps, not averages
		// over the subset of samples that remained valid.
		auto recorded = zero.metrics();
		std::vector<::station::StationRecordingSlot> slots;
		std::vector<::station::StationMetricSample> values;
		for (auto const& h : recorded) { ::station::StationRecordingSlot slot; slot.metric = h.descriptor; slots.push_back(slot); values.push_back(h.samples[0]); }
		::station::StationRecordingLimits limits; limits.samples = 4;
		::station::StationHistory recorder(slots, 1, 100, limits);
		for (int t = 1; t <= 8; ++t)
		{
			for (size_t i = 0; i < slots.size(); ++i)
				if (slots[i].metric.role == ::station::StationMetricRole::Inventory)
					values[i].validity = t == 2 ? ::station::StationMetricValidity::Invalid : ::station::StationMetricValidity::Valid;
			recorder.observe(t, values);
		}
		auto const invalid = recorder.query("battery-energy");
		require(invalid.truncated && invalid.incomplete && std::isnan(invalid.series[0].values[0])
			&& std::isnan(invalid.series[1].upper[0]) && invalid.series[0].coverage[0] == 0.5,
			"coarsening turned an invalid energy interval into a complete stack");
	}
}

namespace station_test::ReportingScenarios
{
	inline void observationBounds()
	{
		// Extreme limits are cheap to reach at the reusable recorder seam, without
		// spending millions of World ticks or introducing another sampling path.
		std::vector<::station::StationRecordingSlot> slots;
		for (size_t i = 0; i < 512; ++i)
		{
			::station::StationRecordingSlot s; auto& d = s.metric;
			d.id = "store/" + std::to_string(i); d.instanceId = std::to_string(i); d.materialId = "water";
			d.equipmentType = "Resource store"; d.exchangeRole = "stored-inventory";
			d.quantity = ::station::StationPhysicalQuantity::Mass; d.unit = ::station::StationCanonicalUnit::Kilogram;
			d.role = ::station::StationMetricRole::Inventory; d.hazardClass = i % 2 ? "normal" : "hazardous";
			slots.push_back(s); s.metric.id += "/usable"; s.metric.exchangeRole = "usable-inventory"; slots.push_back(s);
		}
		::station::StationRecordingLimits limits; limits.samples = 8;
		::station::StationHistory history(slots, 1, 100000, limits);
		require(!history.status().limited && history.status().recordingBytes <= limits.memoryBytes, "high cardinality refused or exceeded bound");
		std::vector<::station::StationMetricSample> values(slots.size());
		for (auto& v : values) { v.actual = 1; v.validity = ::station::StationMetricValidity::Valid; }
		for (int t = 1; t <= 4096; ++t)
		{
			values[0].validity = t == 2 ? ::station::StationMetricValidity::Unavailable : ::station::StationMetricValidity::Valid;
			history.observe(t, values);
		}
		auto const exported = history.metrics();
		require(history.status().coarseningLevel > 1 && history.status().truncated && exported.size() == 1024,
			"long high-cardinality recording did not coarsen together");
		for (auto const& h : exported)
		{
			require(h.samples.size() <= 8 && h.samples.size() == exported[0].samples.size(), "unbounded or unaligned high-cardinality history");
			for (size_t r = 0; r < h.samples.size(); ++r) require(h.samples[r].elapsedSeconds == exported[0].samples[r].elapsedSeconds, "coarsening grid diverged");
		}
		require(!exported[0].samples[0].actual && exported[0].samples[0].coverage < 1
			&& exported[1].samples[0].actual == 1, "coarsening concealed missing span or lost valid zero/constant");
		for (auto const& c : history.catalogue())
		{
			auto const& q = history.query(c.id);
			for (auto const& line : q.series) require(line.times.size() <= 8, "selected query exceeded row bound");
		}
		limits.samples = 1;
		require(::station::StationHistory(slots, 1, 10, limits).status().limited, "minimum two-row failure was not explicit");
		limits = {}; limits.memoryBytes = 4096;
		require(::station::StationHistory(slots, 1, 10, limits).status().limited, "minimum memory failure silently selected subset");
		limits = {}; slots[0].metric.observationNote.assign(limits.metadataBytes + 1, 'x');
		require(::station::StationHistory(slots, 1, 10, limits).status().limited, "long accounting note bypassed metadata bound");
		slots[0].metric.observationNote.clear(); slots.push_back(slots.back());
		require(::station::StationHistory(slots, 1, 10).status().limited, "excessive cardinality silently dropped family");
	}
}

namespace station_test::ReportingScenarios
{
inline ::station::StationSystemsSettings monitorStation()
{
	::station::StationSystemsSettings s;
	s.enabled = true; s.systemsStepWorldTicks = 1;
	::station::AtmosphereSettings room;
	room.id = "room"; room.volumeM3 = 100; room.thermalCapacityKJPerK = 10000;
	room.startingOxygenKg = 20; room.startingNitrogenKg = 80;
	room.minPressureKPa = 1; room.maxPressureKPa = 200;
	s.atmospheres = {room};
	s.monitors = {{"oxygen-low", "atmosphere", "room", "oxygenPartialPressureKPa",
		{17.5, ::station::AlertDirection::Below, 30, 60}, ::station::AlertSeverity::Critical, true}};
	return s;
}
inline void monitorTiming()
{
	::station::ScopedStationStepRatioForTesting clock(std::nullopt);
	for (auto const dt : {0.25f, 1.0f})
	{
		auto settings = monitorStation();
		::station::StationSystemsRuntime run(settings, dt);
		require(run.advanceTo(30 - dt) && run.snapshot().activeAlerts.empty(), "Monitor fired before full on time");
		require(run.advanceTo(30), "fire step failed");
		auto const snap = run.snapshot();
		require(snap.activeAlerts.size() == 1 && snap.closedAlerts.empty(), "Monitor did not fire at on boundary");
		auto const& alert = snap.activeAlerts[0];
		require(alert.firedAtSeconds == 30 && alert.monitorId == "oxygen-low" && alert.subjectType == "atmosphere"
			&& alert.subjectId == "room" && alert.metric == "oxygenPartialPressureKPa"
			&& alert.threshold == 17.5 && alert.severity == ::station::AlertSeverity::Critical
			&& alert.currentValue == snap.atmospheres[0].oxygenPartialPressureKPa,
			"active ledger did not publish observed value, subject, severity or timestamp");
		// Perturb only a live authored parameter; never poke the Monitor or snapshot.
		settings.atmospheres[0].volumeM3 = 50;
		require(run.updateSettings(settings) && run.advanceTo(90 - dt)
			&& run.snapshot().activeAlerts.size() == 1, "Monitor cleared before full recovery time");
		require(run.advanceTo(90), "recovery step failed");
		auto const cleared = run.snapshot();
		require(cleared.activeAlerts.empty() && cleared.closedAlerts.size() == 1
			&& cleared.closedAlerts[0].firedAtSeconds == 30 && cleared.closedAlerts[0].clearedAtSeconds == 90
			&& cleared.closedAlerts[0].clearReason == ::station::AlertClearReason::AutoClear, "clear boundary/ledger wrong");
		require(run.advanceTo(100) && run.snapshot().closedAlerts.size() == 1, "clear repeated");
		require(std::count_if(run.events().begin(), run.events().end(), [](auto const& e) { return e.category == "alert"; }) == 2,
			"fire/clear event stream did not announce both transitions exactly once");
	}
	// Zero-time, strict inequality (equality is inside), both directions,
	// and interrupted persistence in both states.
	auto settings = monitorStation();
	settings.monitors[0].rule.triggerOnSeconds = 0;
	settings.monitors[0].rule.triggerOffSeconds = 0;
	::station::StationSystemsRuntime instant(settings, 1);
	require(instant.snapshot().activeAlerts.empty() && instant.advanceTo(1)
		&& instant.snapshot().activeAlerts.size() == 1, "zero-on did not fire on evaluation");
	settings.atmospheres[0].volumeM3 = 50;
	require(instant.updateSettings(settings) && instant.advanceTo(2) && instant.snapshot().activeAlerts.empty(), "zero-off not instant");
	settings = monitorStation();
	settings.monitors[0].rule = {0, ::station::AlertDirection::Above, 2, 2};
	settings.atmospheres[0].startingOxygenKg = 0;
	::station::StationSystemsRuntime equal(settings, 1);
	require(equal.advanceTo(10) && equal.snapshot().activeAlerts.empty(), "equality fired Above");
	settings.monitors[0].rule.direction = ::station::AlertDirection::Below;
	::station::StationSystemsRuntime equalBelow(settings, 1);
	require(equalBelow.advanceTo(10) && equalBelow.snapshot().activeAlerts.empty(), "equality fired Below");
	settings = monitorStation(); settings.monitors[0].rule = {10, ::station::AlertDirection::Above, 2, 0};
	::station::StationSystemsRuntime above(settings, 1);
	require(above.advanceTo(1) && above.snapshot().activeAlerts.empty() && above.advanceTo(2)
		&& above.snapshot().activeAlerts.size() == 1, "Above direction did not fire at boundary");
	settings = monitorStation(); settings.monitors[0].rule.triggerOnSeconds = 2; settings.monitors[0].rule.triggerOffSeconds = 2;
	::station::StationSystemsRuntime oscillate(settings, 1);
	require(oscillate.advanceTo(1), "oscillation start failed");
	settings.atmospheres[0].volumeM3 = 50;
	require(oscillate.updateSettings(settings) && oscillate.advanceTo(2), "inside sample failed");
	settings.atmospheres[0].volumeM3 = 100;
	require(oscillate.updateSettings(settings) && oscillate.advanceTo(3) && oscillate.snapshot().activeAlerts.empty(), "on persistence was not reset");
	require(oscillate.advanceTo(4) && oscillate.snapshot().activeAlerts.size() == 1, "second on boundary failed");
	settings.atmospheres[0].volumeM3 = 50;
	require(oscillate.updateSettings(settings) && oscillate.advanceTo(5), "first recovery failed");
	settings.atmospheres[0].volumeM3 = 100;
	require(oscillate.updateSettings(settings) && oscillate.advanceTo(6), "outside sample failed");
	settings.atmospheres[0].volumeM3 = 50;
	require(oscillate.updateSettings(settings) && oscillate.advanceTo(7) && oscillate.snapshot().activeAlerts.size() == 1,
		"off persistence was not reset");
	require(oscillate.advanceTo(8) && oscillate.snapshot().closedAlerts[0].clearedAtSeconds == 8, "second off boundary failed");
}
}
