#include "CommandScenarios.h"
#include "CarbonObservationFixture.h"
#include "ConditionScenarios.h"
#include "LiveEditScenarios.h"
#include "station/Replay.h"
#include "serialization/BinarySerializer.h"
#include <iostream>
#include <type_traits>

namespace
{
using station_test::commands::require;
using station::StationSystemsRuntime;
static_assert(std::is_same_v<decltype(std::declval<StationSystemsRuntime&>().effectiveSettings()),
    station::StationSystemsSettings const&>);

void roundTrip(StationSystemsRuntime const& run)
{
    auto const text = station::serializeStationReplay(run);
    std::unique_ptr<StationSystemsRuntime> restored;
    std::string diagnostic;
    if (!station::parseStationReplay(text, restored, &diagnostic))
        throw std::runtime_error(diagnostic);
    require(restored->snapshot() == run.snapshot() && restored->events() == run.events(),
        "Standalone replay lost command state or events");
    require(station::serializeStationReplay(*restored) == text, "Replay journal changed on round trip");
    auto output = serialization::BinarySerializer::toString();
    run.writeFaultReplay(*output); output->serialize();
    auto input = serialization::BinarySerializer::fromString(output->getSerializedString());
    input->deserialize();
    auto binary = StationSystemsRuntime::readFaultReplay(*input, run.effectiveSettings(), 0.25f);
    require(binary->snapshot() == run.snapshot(), "Binary command replay changed state");
    require(restored->advanceTo(run.snapshot().elapsedSeconds + 1), "Restored drive failed");
}

void replayCommands()
{
    auto settings = station_test::commands::repairStation();
    StationSystemsRuntime run(settings, 0.25f);
    require(run.advanceTo(1) && run.injectFault("loop", "coolant-leak", {}), "Injection refused");
    require(run.updateNode<station::CoolantLoopSettings>("loop", [](auto& loop) {
        loop.coolantLeak->releasedKg = 5;
    }), "Live edit refused");
    roundTrip(run); // pending injection, with frozen original booking
    require(run.repairFault("loop", "coolant-leak") && run.advanceTo(3), "Repair failed");
    roundTrip(run); // in-flight reservation/progress
    require(run.advanceTo(12), "Repair completion failed");
    roundTrip(run);

    auto condition = station_test::condition::conditionStation();
    condition.coolantLoops[0].condition->initialCondition = 0.5;
    StationSystemsRuntime recovery(condition, 0.25f);
    require(recovery.restoreCondition("loop", 1), "Condition command refused");
    roundTrip(recovery);
    require(recovery.advanceTo(2), "Condition recovery failed");
    roundTrip(recovery);
}

void tripAndBreaker()
{
    auto settings = station_test::carbonObservationFixture();
    settings.systemsStepWorldTicks = 1;
    settings.sabatierReactors[0].overtempRunaway = {80, 100};
    StationSystemsRuntime run(settings, 0.25f);
    std::string diagnostic;
    require(!run.resetTrip("reactor", &diagnostic) && !diagnostic.empty(), "Unlatched reset accepted");
    require(run.injectFault("reactor", "overtemp-runaway", {})
        && !run.resetTrip("reactor", &diagnostic), "Pending Trip reset accepted");
    roundTrip(run);
    require(run.advanceTo(1) && run.snapshot().sabatierReactors[0].tripped,
        "Injected Trip did not latch");
    auto const stock = run.snapshot().stores;
    require(run.resetTrip("reactor") && !run.snapshot().sabatierReactors[0].tripped
        && run.snapshot().stores == stock, "Trip reset was not free and immediate");
    roundTrip(run);

    station::StationSystemsSettings power; power.enabled = true; power.systemsStepWorldTicks = 1;
    power.powerBuses[0].ratedCapacityKW = 5; power.powerBuses[0].tripPersistenceSeconds = 1;
    station::SolarArraySettings solar; solar.id = "sun"; solar.capacityKW = 20;
    power.solarArrays = {solar};
    station::EquipmentLoadSettings load; load.id = "load"; load.electricalDemandKW = 8;
    load.thermalClass = "moderate";
    power.equipmentLoads = {load};
    StationSystemsRuntime breaker(power, 0.25f);
    require(!breaker.breakerReset("missing", &diagnostic) && !diagnostic.empty()
        && !breaker.breakerReset("main", &diagnostic), "Invalid breaker reset accepted");
    require(breaker.advanceTo(1) && breaker.snapshot().power.buses[0].tripped, "Breaker did not latch");
    roundTrip(breaker);
    require(breaker.updateNode<station::EquipmentLoadSettings>("load", [](auto& entry) {
        entry.electricalDemandKW = 4;
    }) && breaker.advanceTo(2) && breaker.snapshot().power.buses[0].tripped,
        "Breaker latch auto-cleared");
    require(breaker.breakerReset("main") && !breaker.snapshot().power.buses[0].tripped,
        "Breaker reset failed");
    roundTrip(breaker);
    require(breaker.advanceTo(3) && breaker.snapshot().power.buses[0].suppliedKW == 4,
        "Breaker reset did not restore service");
}

void ordinaryLiveReplay()
{
    station::StationSystemsSettings settings; settings.enabled = true; settings.systemsStepWorldTicks = 1;
    station::ResourceStoreSettings source; source.id = "source"; source.unit = "kg";
    source.capacity = source.startingQualified = 100;
    auto sink = source; sink.id = "sink"; sink.startingQualified = 0;
    settings.stores = {source, sink};
    station::ResourceTransferSettings transfer;
    transfer.id = "transfer"; transfer.sourceId = "source"; transfer.destinationId = "sink";
    transfer.unit = "kg"; transfer.ratePerSecond = 1; settings.transfers = {transfer};
    StationSystemsRuntime run(settings, 0.25f);
    require(run.advanceTo(2) && run.updateNode<station::ResourceTransferSettings>("transfer", [](auto& entry) {
        entry.ratePerSecond = 2;
    }) && run.advanceTo(4), "Ordinary live transfer failed");
    require(run.snapshot().stores[1].currentQualified == 6, "Live rate applied retroactively");
    roundTrip(run); // no fault opt-in or operational command: settings still need a journal
}

void transactionalEdits()
{
    StationSystemsRuntime run(station_test::commands::repairStation(), 0.25f);
    require(run.advanceTo(1), "Edit fixture failed");
    auto const before = station::serializeStationReplay(run);
    auto const settings = run.effectiveSettings();
    auto const snap = run.snapshot();
    std::string diagnostic;
    require(!run.updateNode<station::ResourceStoreSettings>("parts", [](auto& store) {
        store.id = "renamed"; store.capacity = 500;
    }, &diagnostic) && !diagnostic.empty(), "Node identity changed live");
    try
    {
        run.updateNode<station::CoolantLoopSettings>("loop", [](auto& loop) {
            loop.ratedFlowKgPerS = 100;
            throw std::runtime_error("callback failure");
        });
        require(false, "Callback exception disappeared");
    }
    catch (std::runtime_error const& error)
    { require(std::string(error.what()) == "callback failure", "Unexpected exception"); }
    // A refusal in a late collection must not apply the earlier store edit.
    auto offered = settings;
    offered.stores[0].capacity = 500;
    offered.atmospheres[0].id = "renamed";
    require(!run.updateSettings(offered, &diagnostic) && !diagnostic.empty(), "Bulk rename accepted");
    require(run.effectiveSettings() == settings && run.snapshot() == snap
        && station::serializeStationReplay(run) == before, "Refused edit changed state or journal");

    auto links = settings;
    station::AtmospherePathwaySettings hatch;
    hatch.id = "hatch"; hatch.aAtmosphereId = "room";
    hatch.bAtmosphereId = station::StationExteriorAtmosphereId;
    links.atmospherePathways = {hatch};
    station::ThermalLinkSettings link; link.id = "link";
    link.aAtmosphereId = "room"; link.bAtmosphereId = "other"; link.conductanceKWPerK = 1;
    auto other = links.atmospheres[0]; other.id = "other"; links.atmospheres.push_back(other);
    links.thermalLinks = {link};
    StationSystemsRuntime connected(links, 0.25f);
    auto const bytes = station::serializeStationReplay(connected);
    require(!connected.updateNode<station::AtmospherePathwaySettings>("hatch", [](auto& entry) {
        entry.bAtmosphereId = "other"; entry.areaM2 = 50;
    }, &diagnostic) && !diagnostic.empty(), "Typed pathway endpoint changed live");
    require(!connected.updateNode<station::ThermalLinkSettings>("link", [](auto& entry) {
        entry.bAtmosphereId = "room"; entry.conductanceKWPerK = 10;
    }, &diagnostic) && !diagnostic.empty(), "Typed Thermal link endpoint changed live");
    require(station::serializeStationReplay(connected) == bytes, "Refused endpoint edit changed journal");
}

void sourceAndMalformedReplay()
{
    station::StationSystemsSettings settings; settings.enabled = true;
    settings.systemsStepWorldTicks = 1;
    std::string const source = "return function() return {advance=function(self,tick) return tick end} end";
    StationSystemsRuntime run(settings, 0.25f, source);
    require(run.advanceTo(1), "Explicit-source drive failed");
    auto text = station::serializeStationReplay(run);
    std::unique_ptr<StationSystemsRuntime> restored;
    std::string diagnostic;
    if (!station::parseStationReplay(text, restored, &diagnostic, source))
        throw std::runtime_error(diagnostic);
    require(restored->snapshot() == run.snapshot(), "Explicit-source replay changed state");
    auto* const original = restored.get();
    for (std::string const bad : {std::string{}, std::string{"invalid Lua"},
        std::string{"return function() return {advance=function() error('replay sentinel') end} end"}})
        require(!station::parseStationReplay(text, restored, &diagnostic, bad)
            && !diagnostic.empty() && restored.get() == original, "Bad source replaced destination");
    for (auto const& bad : {std::string{}, std::string{"[malformed"},
        std::string{"schemaVersion: 99999"}, std::string{"schemaVersion: 137"}})
        require(!station::parseStationReplay(bad, restored, &diagnostic)
            && !diagnostic.empty() && restored.get() == original, "Bad replay replaced destination");
    auto const at = text.find("completedSteps: 4");
    require(at != std::string::npos, "Missing replay step count");
    text.replace(at, std::string("completedSteps: 4").size(), "completedSteps: 0");
    // A zero-step journal is valid; commands out of range are checked below.
    require(station::parseStationReplay(text, restored, &diagnostic, source)
        && restored->snapshot().elapsedSeconds == 0, "Zero-step replay failed");
    StationSystemsRuntime fault(station_test::commands::repairStation(), 0.25f);
    require(fault.advanceTo(1) && fault.injectFault("loop", "coolant-leak", {}), "Fault setup failed");
    text = station::serializeStationReplay(fault);
    auto pos = text.find("afterStep: 4"); require(pos != std::string::npos, "Missing command boundary");
    text.replace(pos, std::string("afterStep: 4").size(), "afterStep: 5");
    auto* const unchanged = restored.get();
    require(!station::parseStationReplay(text, restored, &diagnostic)
        && !diagnostic.empty() && restored.get() == unchanged, "Out-of-order command accepted");
}
}
int main()
{
    try
    {
        station_test::live::liveSettingsSeam();
        station_test::commands::faultOutages();
        station_test::commands::repairLifecycle();
        station_test::commands::repairInterruption();
        station_test::condition::conditionStress();
        station_test::condition::conditionCrossing();
        station_test::condition::conditionRestore();
        station_test::condition::conditionTransactions();
        transactionalEdits(); ordinaryLiveReplay(); replayCommands(); tripAndBreaker(); sourceAndMalformedReplay();
    }
    catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
