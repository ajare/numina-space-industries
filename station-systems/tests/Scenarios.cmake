# Existing scenario assertions compiled solely against the public library.
add_executable(station-scenario-consumer
    ScenarioConsumer.cpp
    support/Smoke.cpp support/StandaloneFixtures.cpp support/StationRuntimeFixtures.cpp
    scenarios/StationInvarianceChecks.cpp
    scenarios/StationPhysicalFidelityChecks.cpp
    scenarios/StationPowerChecks.cpp
    scenarios/StationProtectionChecks.cpp
    scenarios/StationRuntimeRegressionChecks.cpp
    scenarios/StationSharedSupplyChecks.cpp
    scenarios/StationTransferSupplyChecks.cpp
)
target_link_libraries(station-scenario-consumer PRIVATE StationSystems::StationSystems)
target_include_directories(station-scenario-consumer PRIVATE support scenarios)
target_compile_definitions(station-scenario-consumer PRIVATE
    PF_SMOKE_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
    PF_SMOKE_SHARED_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
foreach(check IN ITEMS
    invariance
    invarianceRegistry
    invarianceRepresentative
    invarianceProfiles
    invarianceProfileRollback
    invarianceGeneratorTiming
    invarianceGeneratorFunding
    invarianceLongRun
    invariancePerformance
    radiatorSI
    physicalCoupling
    generatorExhaustion
    generatorOutage
    generatorPortCapacity
    recoveryConvergence
    benchmarkWorkloads
    powerChainSeam
    thermalReservoirSeam
    radiatorSeam
    rateAwareProtection
    protectionReallocation
    protectionFreeze
    protectionGrossExcursion
    microbialSupplyRoundoff
    historyCadencePrecision
    sharedSupplySplitRatio
    sharedSupplyZeroSpare
    sharedSupplyCombinedShortfall
    sharedSupplyMinimumOperable
    sharedSupplyRecomputed
    sharedSupplyCombinedDemo
    sharedSupplyReplay
    sharedSupplyProtectedSpare
    sharedSupplyFiniteFeeds
    transferLossyPrimary
    transferLossyCycles
    transferContention
    transferMultipleContention
    transferContentionDwell
    transferHoldsPrimary
    transferFailover
    transferAlertAutoClears
    transferAlertOccurrences
    transferDwellReturn
    transferZeroDwell
    transferNoFlapping
    transferBackupAlsoShort
    transferNoThirdPath
    transferReplay
    transferPerLoadDwell
)
    add_test(NAME station-library-scenario-${check}
        COMMAND station-scenario-consumer --check ${check})
    set_tests_properties(station-library-scenario-${check} PROPERTIES
        LABELS "station;functional;library-scenario" TIMEOUT 300)
endforeach()
# Keep the established long-run budgets and isolate measured performance work.
set_tests_properties(station-library-scenario-invarianceLongRun PROPERTIES TIMEOUT 240)
set_tests_properties(station-library-scenario-benchmarkWorkloads PROPERTIES RUN_SERIAL TRUE)
