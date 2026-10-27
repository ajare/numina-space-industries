#include "ReportingScenarios.h"
#include "station/Persistence.h"
#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
void referenceReporting()
{
    using namespace station;
    using station_test::ReportingScenarios::require;
    std::ifstream file(std::string(STATION_FIXTURE_ROOT) + "/resources/station-systems/long-run-workload.yaml");
    require(file.good(), "Missing established stability workload");
    std::string text{std::istreambuf_iterator<char>(file), {}};
    StationSystemsSettings settings;
    std::string diagnostic;
    require(parseStationConfiguration(text, settings, &diagnostic), diagnostic.c_str());
    StationSystemsRuntime runtime(settings, 1.0f / 60.0f);
    for (double target : {60.0, 120.0, 300.0})
    {
        require(runtime.advanceTo(target), "Stability workload failed");
        auto const snapshot = runtime.snapshot();
        auto const revision = runtime.observationRevision();
        auto const histories = runtime.metrics();
        auto const events = runtime.events();
        auto const audit = auditStationStability(snapshot, events, runtime.trends(), runtime.diagnostics());
        require(audit.clean && audit.finiteChecks > 0 && audit.ledgerChecks > 0,
            "Public workload audit failed");
        require(events.size() <= StationEventHistoryLimit, "Event retention exceeded bound");
        require(!runtime.recordingStatus().limited && !histories.empty(), "Workload reporting unavailable");
        for (auto const& chart : runtime.catalogue())
        {
            auto const result = runtime.query(chart.id);
            auto const expected = queryStationChart(chart, histories);
            require(result.series.size() == expected.series.size(), "Public selected/exported chart mismatch");
            for (size_t i = 0; i < result.series.size(); ++i)
            {
                auto const& a = result.series[i]; auto const& b = expected.series[i];
                require(a.id == b.id && a.times == b.times && a.values.size() == b.values.size(), "Chart identity/timeline mismatch");
                for (size_t row = 0; row < a.values.size(); ++row)
                    require(a.values[row] == b.values[row] || (std::isnan(a.values[row]) && std::isnan(b.values[row])), "Chart values mismatch");
            }
            auto const work = runtime.queryWork();
            (void)runtime.query(chart.id);
            require(runtime.queryWork().queries == work.queries, "Unchanged query missed cache");
        }
        require(runtime.snapshot() == snapshot && runtime.events() == events && runtime.observationRevision() == revision,
            "Reporting/audit changed runtime state");
        require(runtime.advanceTo(snapshot.elapsedSeconds) && runtime.observationRevision() == revision,
            "Reached target changed reporting revision");
    }
}
}
int main()
{
    try
    {
        station_test::ReportingScenarios::batteryObservations();
        station_test::ReportingScenarios::observationBounds();
        station_test::ReportingScenarios::monitorTiming();
        referenceReporting();
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
