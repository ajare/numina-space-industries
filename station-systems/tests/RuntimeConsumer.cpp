#include "ClockScenarios.h"
#include "EventSplitScenarios.h"
#include "station/StationMetrics.h"
#include "serialization/BinarySerializer.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool ok, char const* message)
{
    if (!ok) throw std::runtime_error(message);
}
void sourceAndReplay()
{
    station::StationSystemsSettings settings;
    settings.enabled = true;
    constexpr float quantum = 1.0f / 60.0f;
    // Separate explicit factories, each with its own private state and outcome.
    std::string const good = "return function() return {advance=function(self, tick) return tick end} end";
    std::string const failing = "return function() return {advance=function() error('consumer sentinel') end} end";
    station::StationSystemsRuntime first(settings, quantum, good);
    station::StationSystemsRuntime second(settings, quantum, failing);
    station::StationSystemsRuntime embedded(settings, quantum);
    require(first.advanceTo(1), "Injected factory failed");
    require(!second.advanceTo(1), "Failing injected factory fell back to default");
    require(!second.diagnostics().empty(), "Script failure was not observable");
    require(embedded.advanceTo(1) && embedded.snapshot().healthy, "Embedded default unavailable");
    require(first.snapshot().healthy, "Other runtime source/state was changed");
    for (std::string const bad : {std::string{}, std::string{"not valid Lua"}})
    {
        bool refused = false;
        try { station::StationSystemsRuntime runtime(settings, quantum, bad); }
        catch (std::exception const&) { refused = true; }
        require(refused, "Invalid explicit source silently selected default");
    }
    auto output = serialization::BinarySerializer::toString();
    embedded.writeFaultReplay(*output);
    output->serialize();
    auto input = serialization::BinarySerializer::fromString(output->getSerializedString());
    input->deserialize();
    auto restored = station::StationSystemsRuntime::readFaultReplay(*input, settings, quantum);
    require(restored->snapshot() == embedded.snapshot(), "Standalone runtime replay changed snapshot");
}
}
int main()
{
    try
    {
        station_test::ClockScenarios::clock();
        station_test::EventSplitScenarios::eventSplit();
        station_test::EventSplitScenarios::saturationCrossing();
        station_test::EventSplitScenarios::eventOrder();
        station_test::EventSplitScenarios::eventRollback();
        station_test::EventSplitScenarios::maintenanceSplit();
        sourceAndReplay();
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
