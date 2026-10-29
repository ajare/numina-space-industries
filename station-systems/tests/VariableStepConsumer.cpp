#include "station/StationSystemsRuntime.h"
#include "station/Replay.h"
#include "serialization/BinarySerializer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace station;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a, double b, const char* message = "Numeric mismatch") {
    if (!std::isfinite(a) || std::abs(a - b) > 1e-9 * std::max(1.0, std::abs(b)))
        throw std::runtime_error(message);
}
StationSystemsSettings scene() {
    StationSystemsSettings s;
    s.enabled = true;
    s.systemsStepWorldTicks = 1;
    s.observationIntervalSeconds = 0.1;
    s.stores = {{"source", "kg", 100, 50, 0}, {"sink", "kg", 100, 0, 0}};
    ResourceTransferSettings transfer;
    transfer.id = "pump"; transfer.sourceId = "source"; transfer.destinationId = "sink";
    transfer.unit = "kg"; transfer.ratePerSecond = 0.1;
    s.transfers = {transfer};
    return s;
}
void clocksAndDeltas() {
    auto s = scene();
    StationSystemsRuntime fixed(s, 0.125f);
    auto const beforeFixed = fixed.snapshot();
    require(!fixed.update(0.1) && fixed.snapshot() == beforeFixed, "Fixed mode accepted variable update");
    require(fixed.advanceTo(1), "Legacy fixed drive broke");
    near(fixed.snapshot().elapsedSeconds, 1);
    StationSystemsRuntime runtime(s, StationSystemsUpdateMode::Variable);
    require(runtime.updateMode() == StationSystemsUpdateMode::Variable, "Wrong mode");
    require(runtime.snapshot().systemsStepWorldTicks == 0 && runtime.snapshot().systemsTimestepSeconds == 0,
            "Variable snapshot invented a World quantum");
    auto const before = runtime.snapshot();
    auto const revision = runtime.observationRevision();
    require(!runtime.advanceTo(1) && runtime.snapshot() == before, "Variable mode accepted fixed drive");
    for (double bad : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::max()}) {
        require(!runtime.update(bad), "Invalid delta accepted");
        require(runtime.snapshot() == before && runtime.observationRevision() == revision,
                "Rejected update changed committed state");
    }
    double elapsed = 0;
    uint64_t steps = 0;
    for (double dt : {0.01, 0.05, 0.1, 0.2, 1.0, 0.01, 1.0}) {
        require(runtime.update(dt), "Variable update failed");
        elapsed += dt;
        auto snap = runtime.snapshot();
        require(snap.worldTick == ++steps, "Update did not perform exactly one Station step");
        near(snap.elapsedSeconds, elapsed);
        near(snap.systemsTimestepSeconds, dt);
        near(snap.stores[1].currentQualified, elapsed * 0.1, "Transfer used wrong dt");
        near(snap.stores[0].currentQualified + snap.stores[1].currentQualified, 50, "Lost material");
    }
    require(!runtime.update(1e-30), "Unrepresentable time increment accepted");
    for (auto const& t : runtime.trends()) {
        require(t.sampleIntervalSeconds == 0, "Irregular history advertised a fixed cadence");
        require(!t.samples.empty() && t.samples.back().worldTick == steps, "Trend invented step id");
        near(t.samples.back().elapsedSeconds, elapsed);
    }
    // Ten thousand small increments exercise accumulated-time drift and switching
    // from large to small deltas without retrospectively changing past time.
    for (int i = 0; i < 10000; ++i) require(runtime.update(0.01), "Long variable run failed");
    near(runtime.snapshot().elapsedSeconds, elapsed + 100);
    auto disabled = s; disabled.enabled = false;
    StationSystemsRuntime idle(disabled, StationSystemsUpdateMode::Variable);
    auto const idleBefore = idle.snapshot();
    require(idle.update(1) && idle.snapshot() == idleBefore, "Disabled update advanced time");
}
void lifecycle() {
    auto s = scene();
    // Every independent lifecycle sandbox must receive the exact external dt.
    const std::string source = R"(return function()
        local expected = {0.01, 0.05, 0.1, 0.2, 1}
        return {advance=function(self, tick, dt)
            assert(math.abs(dt - expected[tick]) < 1e-12, 'wrong delta')
            return tick
        end}
    end)";
    StationSystemsRuntime runtime(s, StationSystemsUpdateMode::Variable, source);
    for (double dt : {0.01, 0.05, 0.1, 0.2, 1.0}) require(runtime.update(dt), "Lifecycle did not receive supplied delta");
    std::unique_ptr<StationSystemsRuntime> loaded;
    std::string diagnostic;
    require(parseStationReplay(serializeStationReplay(runtime), loaded, &diagnostic, source)
        && loaded->snapshot() == runtime.snapshot(), "Explicit-source variable replay changed lifecycle deltas");
    StationSystemsRuntime failed(s, StationSystemsUpdateMode::Variable,
        "return function() return {advance=function() error('variable sentinel') end} end");
    auto const before = failed.snapshot();
    auto const revision = failed.observationRevision();
    require(!failed.update(1), "Script failure was committed");
    require(!failed.snapshot().healthy && failed.snapshot().elapsedSeconds == 0
        && failed.snapshot().stores == before.stores && failed.snapshot().systemsTimestepSeconds == 0,
        "Failed variable step leaked state/time");
    require(!failed.update(0.1) && failed.observationRevision() == revision, "Failed run was retried");
    require(!failed.diagnostics().empty(), "Failure lacked diagnostics");
}
void eventsAndRollback() {
    auto s = scene();
    s.stores[0].startingQualified = 0;
    s.transfers[0].ratePerSecond = 1;
    s.deliveries = {{"pulse", "source", "kg", 1, 0.005, 0.025}};
    s.wasteEjections = {{"drain", "sink", "kg", 0.001, 0.015, 0.05}};
    StationSystemsRuntime runtime(s, StationSystemsUpdateMode::Variable);
    require(runtime.update(0.01) && runtime.update(0.2) && runtime.update(1), "Event update failed");
    auto snap = runtime.snapshot();
    near(snap.stores[1].currentQualified, snap.elapsedSeconds - 0.005 - snap.wasteEjections[0].ejectedTotal,
         "Scheduled boundaries shifted with variable dt");
    near(snap.stores[0].currentQualified + snap.stores[1].currentQualified + snap.wasteEjections[0].ejectedTotal,
         snap.deliveries[0].acceptedTotal, "Schedule did not conserve mass");
    // A capacity/reserve crossing inside one long external update.
    s = scene();
    s.stores[0].startingQualified = 0.0137;
    StationSystemsRuntime saturating(s, StationSystemsUpdateMode::Variable);
    require(saturating.update(1), "Saturation update failed");
    near(saturating.snapshot().stores[1].currentQualified, 0.0137);
    // A later sub-interval overflows, so all earlier event bookings roll back.
    s = scene();
    s.stores[0].capacity = 1e308; s.stores[0].startingQualified = 0;
    s.deliveries = {{"huge", "source", "kg", 1e308, 0.005, 0.025}};
    s.wasteEjections = {{"drain", "source", "kg", 1e308, 0.015}};
    StationSystemsRuntime failing(s, StationSystemsUpdateMode::Variable);
    auto const before = failing.snapshot();
    auto const events = failing.events();
    require(!failing.update(0.5), "Overflowing ledger update committed");
    require(failing.snapshot().elapsedSeconds == 0 && failing.snapshot().stores == before.stores
        && failing.snapshot().deliveries == before.deliveries && failing.events() == events,
        "Failed ledger update leaked an interval");
}
void atmosphereDeltas() {
    auto s = scene();
    AtmosphereSettings room;
    room.id = "room"; room.volumeM3 = 100; room.thermalCapacityKJPerK = 10000;
    room.startingOxygenKg = 20; room.startingNitrogenKg = 80;
    room.minPressureKPa = 1; room.maxPressureKPa = 200;
    room.occupants = 1; room.oxygenUsePerOccupantKgPerS = 0.001;
    room.co2ProductionPerOccupantKgPerS = 0.001;
    room.sensibleHeatPerOccupantKW = 0.1;
    s.atmospheres = {room};
    std::optional<double> referenceTemperature;
    for (double dt : {0.01, 0.05, 0.1, 0.2, 1.0}) {
        StationSystemsRuntime run(s, StationSystemsUpdateMode::Variable);
        int const count = static_cast<int>(std::lround(10 / dt));
        for (int i = 0; i < count; ++i) require(run.update(dt), "Atmosphere delta update failed");
        auto const a = run.snapshot().atmospheres[0];
        near(a.oxygenKg, room.startingOxygenKg - 0.01, "Oxygen load did not integrate delta");
        near(a.carbonDioxideKg, room.startingCarbonDioxideKg + 0.01, "CO2 load did not integrate delta");
        if (!referenceTemperature) referenceTemperature = a.temperatureC;
        near(a.temperatureC, *referenceTemperature, "Isolated heat load changed across supported deltas");
    }
}
void observations() {
    auto s = scene();
    s.observationIntervalSeconds = 0.25f;
    StationSystemsRuntime run(s, StationSystemsUpdateMode::Variable);
    require(run.update(0.1) && run.update(0.1), "Observation setup failed");
    require(run.metrics()[0].samples.empty(), "Observed before due simulated time");
    require(run.update(0.1) && run.update(1.0), "Observation crossing failed");
    auto const rows = run.metrics()[0].samples;
    require(rows.size() == 2, "Invented intermediate observations when crossing several boundaries");
    near(rows[0].elapsedSeconds, 0.3);
    near(rows[1].elapsedSeconds, 1.3);
    auto const revision = run.observationRevision();
    require(run.update(0.01) && run.observationRevision() == revision, "Unreached observation boundary mutated history");
    bool foundRate = false;
    for (auto const& metric : run.metrics()) {
        if (metric.descriptor.instanceId != "pump" || metric.descriptor.unit != StationCanonicalUnit::KilogramPerSecond)
            continue;
        for (auto const& sample : metric.samples) if (sample.actual) {
            near(*sample.actual, metric.descriptor.role == StationMetricRole::Loss ? 0.0 : 0.1,
                 "Rate metric divided by the wrong duration");
            if (metric.descriptor.role != StationMetricRole::Loss) foundRate = true;
        }
    }
    require(foundRate, "Missing transfer rate observation");
    s.observationIntervalSeconds = 0.125f;
    require(run.updateSettings(s) && run.update(0.065), "Live observation cadence edit failed");
    require(run.observationRevision() != revision, "Live cadence did not use simulated-time boundary");
}
void monitorsAndFaults() {
    auto s = scene();
    AtmosphereSettings room;
    room.id = "room"; room.volumeM3 = 100; room.thermalCapacityKJPerK = 10000;
    room.startingOxygenKg = 20; room.startingNitrogenKg = 80;
    room.minPressureKPa = 1; room.maxPressureKPa = 200;
    room.fire = AtmosphereFireSettings{0.001, 0.0001, 0.001, 0.01, 0.1};
    s.atmospheres = {room};
    s.monitors = {{"oxygen-low", "atmosphere", "room", "oxygenPartialPressureKPa",
        {17.5, AlertDirection::Below, 1.25, 1.25}, AlertSeverity::Critical, true}};
    StationSystemsRuntime run(s, StationSystemsUpdateMode::Variable);
    require(run.update(1) && run.update(0.125) && run.snapshot().activeAlerts.empty(),
            "Monitor fired before variable persistence interval");
    require(run.update(0.125) && run.snapshot().activeAlerts.size() == 1,
            "Monitor multiplied sample count by last dt instead of elapsed duration");
    near(run.snapshot().activeAlerts[0].firedAtSeconds, 1.25);
    s.atmospheres[0].volumeM3 = 50;
    require(run.updateSettings(s), "Monitor recovery edit failed");
    require(run.update(1) && run.update(0.125) && run.snapshot().activeAlerts.size() == 1,
            "Monitor cleared before variable recovery interval");
    require(run.update(0.125) && run.snapshot().activeAlerts.empty(), "Variable Monitor did not clear");
    near(*run.snapshot().closedAlerts[0].clearedAtSeconds, 2.5);
    require(run.injectFault("room", "fire", {}), "Variable fault injection failed");
    require(run.update(0.01) && run.update(1), "Variable fault update failed");
    require(run.snapshot().atmospheres[0].fireOxygenConsumedKg > 0, "Fault did not integrate supplied dt");
    std::unique_ptr<StationSystemsRuntime> loaded;
    std::string diagnostic;
    require(parseStationReplay(serializeStationReplay(run), loaded, &diagnostic), diagnostic.c_str());
    // Monitor observations deliberately restart on load; physical state and
    // fault accounting must nevertheless reconstruct exactly.
    require(loaded->snapshot().atmospheres == run.snapshot().atmospheres,
            "Fault replay lost variable command boundary");
}
void replay() {
    auto s = scene();
    StationSystemsRuntime runtime(s, StationSystemsUpdateMode::Variable);
    require(runtime.hasFaultReplayState(), "Variable mode omitted replay state");
    for (double dt : {0.01, 0.01, 1.0, 0.2}) require(runtime.update(dt), "Replay setup failed");
    auto changed = s;
    changed.transfers[0].ratePerSecond = 0.3;
    require(runtime.updateSettings(changed), "Replay settings update failed");
    for (double dt : {0.05, 0.1, 0.2, 1.0}) require(runtime.update(dt), "Replay setup failed");
    auto const text = serializeStationReplay(runtime);
    std::unique_ptr<StationSystemsRuntime> restored;
    std::string diagnostic;
    require(parseStationReplay(text, restored, &diagnostic), diagnostic.c_str());
    require(restored->snapshot() == runtime.snapshot(), "YAML replay lost variable partition or commands");
    require(restored->updateMode() == StationSystemsUpdateMode::Variable, "Replay lost mode");
    require(restored->metrics().size() == runtime.metrics().size(), "Replay lost history");
    auto const a = restored->metrics(), b = runtime.metrics();
    for (size_t i = 0; i < a.size(); ++i) {
        require(a[i].samples.size() == b[i].samples.size(), "Replay lost sample count");
        for (size_t j = 0; j < a[i].samples.size(); ++j) {
            auto const& x = a[i].samples[j]; auto const& y = b[i].samples[j];
            require(x.elapsedSeconds == y.elapsedSeconds && x.actual == y.actual && x.requested == y.requested
                && x.coverage == y.coverage && x.validity == y.validity, "Replay changed timestamped history");
        }
    }
    auto output = serialization::BinarySerializer::toString();
    runtime.writeFaultReplay(*output); output->serialize();
    auto input = serialization::BinarySerializer::fromString(output->getSerializedString());
    input->deserialize();
    auto binary = StationSystemsRuntime::readFaultReplay(*input, runtime.effectiveSettings(), 0.1f);
    require(binary->snapshot() == runtime.snapshot(), "Binary replay lost variable partition");
    require(restored->update(0.01) && runtime.update(0.01), "Replay continuation failed");
    require(restored->snapshot() == runtime.snapshot(), "Replay continuation diverged");
    // Corrupt duration and count without touching the output pointer on failure.
    for (auto const& pair : {std::pair<std::string, std::string>{"seconds: 0.01", "seconds: -0.01"},
                            {"steps: 2", "steps: 200000"}}) {
        auto bad = text;
        auto const at = bad.find(pair.first);
        require(at != std::string::npos, "Missing duration fixture text");
        bad.replace(at, pair.first.size(), pair.second);
        auto* previous = restored.get();
        require(!parseStationReplay(bad, restored, &diagnostic) && restored.get() == previous,
                "Malformed variable replay accepted or changed output");
    }
    StationSystemsRuntime fixed(s, 0.125f);
    require(fixed.advanceTo(1), "Fixed replay setup failed");
    auto legacy = serializeStationReplay(fixed);
    const auto currentVersion="schemaVersion: "+std::to_string(kStationSchemaVersion);
    auto const version = legacy.find(currentVersion);
    require(version != std::string::npos, "Missing replay version");
    legacy.replace(version, currentVersion.size(), "schemaVersion: 138");
    require(parseStationReplay(legacy, restored, &diagnostic) && restored->snapshot() == fixed.snapshot(),
            "Legacy fixed replay no longer loads");
    require(restored->advanceTo(2) && fixed.advanceTo(2) && restored->snapshot() == fixed.snapshot(),
            "Legacy fixed replay continuation changed");
    StationSystemsRuntime empty(s, StationSystemsUpdateMode::Variable);
    require(parseStationReplay(serializeStationReplay(empty), restored, &diagnostic)
        && restored->snapshot() == empty.snapshot(), "Empty variable replay failed");
}
void irregularHistory() {
    StationRecordingSlot slot;
    slot.subsystem = "test"; slot.metric.id = "variable";
    StationRecordingLimits limits; limits.samples = 2;
    StationHistory history({slot}, 0.1, 100, limits, 0.1, true);
    auto observe = [&](double t, double value, uint64_t tick) {
        StationMetricSample sample;
        sample.actual = value; sample.validity = StationMetricValidity::Valid;
        history.observe(t, {sample}, tick);
    };
    observe(0.1, 10, 1);
    observe(1.0, 20, 2);
    observe(1.1, 30, 3); // coarsens first pair, pending next bucket
    auto metrics = history.metrics();
    near(*metrics[0].samples[0].actual, 19, "Coarsening ignored irregular interval weights");
    require(metrics[0].sampleIntervalSeconds == 0 && history.status().resolutionSeconds == 0,
            "Irregular resolution not advertised");
    observe(2.0, 40, 4);
    auto const trend = history.trends().at(0);
    require(trend.samples.size() == 2 && trend.samples[0].worldTick == 2 && trend.samples[1].worldTick == 4,
            "Coarsening lost endpoint step ids");
    near(trend.samples[1].actual, 39);
    near(trend.samples[1].elapsedSeconds, 2);
}
}
int main() {
    try {
        clocksAndDeltas(); lifecycle(); eventsAndRollback(); atmosphereDeltas(); observations(); monitorsAndFaults(); replay(); irregularHistory();
        std::cout << "Variable-duration runtime passed\n";
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
