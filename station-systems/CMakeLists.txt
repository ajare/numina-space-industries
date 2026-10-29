cmake_minimum_required(VERSION 3.24)
project(StationSystems VERSION 0.1.0 LANGUAGES C CXX)

# Standalone builds provide the same generic serialization layer as core.
if(NOT TARGET Promethium::Serialization)
    find_package(yaml-cpp CONFIG QUIET)
    if(NOT TARGET yaml-cpp::yaml-cpp AND NOT TARGET yaml-cpp)
        include(FetchContent)
        set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(yaml-cpp
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
            GIT_TAG 8eb618e889c003457d73f7c54777603835677ff4)
        FetchContent_MakeAvailable(yaml-cpp)
    endif()
    add_subdirectory(../serialization "${CMAKE_CURRENT_BINARY_DIR}/serialization")
endif()

if(NOT TARGET nsi-lua)
    include(cmake/Lua.cmake)
endif()

# The library owns its default script and invalidates embedding on source edits.
set(NSI_STATION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/lifecycle.lua")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${NSI_STATION_SOURCE}")
file(READ "${NSI_STATION_SOURCE}" NSI_STATION_SYSTEMS_LUA)
configure_file(src/BundledStationSystemsSource.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/BundledStationSystemsSource.h" @ONLY)

add_library(StationSystems STATIC
    src/AtmosphereMetrics.cpp
    src/Configuration.cpp
    src/ConfigurationText.cpp
    src/MonitorCatalogue.cpp
    src/Persistence.cpp
    src/Replay.cpp
    src/ScriptSandbox.cpp
    src/StationAlgaeMetrics.cpp
    src/StationAtmosphereMetrics.cpp
    src/StationBusBreakers.cpp
    src/StationCarbonMetrics.cpp
    src/StationConnections.cpp
    src/StationCrewAtmosphereMetrics.cpp
    src/StationCropMetrics.cpp
    src/StationHistory.cpp
    src/StationMetrics.cpp
    src/StationMicrobialMetrics.cpp
    src/StationObservableInvariance.cpp
    src/StationPorts.cpp
    src/StationPrefab.cpp
    src/StationScheduledMetrics.cpp
    src/StationStability.cpp
    src/StationStoreConnectionMonitors.cpp
    src/StationSystems.cpp
    src/StationSystemsFaults.cpp
    src/StationSystemsForecast.cpp
    src/StationSystemsLedger.cpp
    src/StationSystemsLiveSettings.cpp
    src/StationSystemsMonitors.cpp
    src/StationSystemsPhysics.cpp
    src/StationGasFlow.cpp
    src/CellFluid.cpp
    src/CellAerodynamics.cpp
    src/CellThermal.cpp
    src/ThermalBody.cpp
    src/StationSystemsSnapshot.cpp
    src/StationSystemsTrends.cpp
    src/StationThermalMetrics.cpp
    src/StationTickAtmosphere.cpp
    src/StationTickAuditPower.cpp
    src/StationTickAuditState.cpp
    src/StationTickBiology.cpp
    src/StationTickCommitAndEvents.cpp
    src/StationTickCondition.cpp
    src/StationTickGeneration.cpp
    src/StationTickHelpers.cpp
    src/StationTickPrepare.cpp
    src/StationTickProcessing.cpp
    src/StationTickProtection.cpp
    src/StationTickThermal.cpp
    src/StationTickTransferSupply.cpp
    src/StationTickWear.cpp
    src/StationWaterGasMetrics.cpp
    src/Validation.cpp
)
add_library(StationSystems::StationSystems ALIAS StationSystems)
target_compile_features(StationSystems PUBLIC cxx_std_20)
# The shared source-level script support headers include lua.hpp. Consumers
# must receive that requirement through the public target too.
target_link_libraries(StationSystems PUBLIC Promethium::Serialization nsi-lua)
target_include_directories(StationSystems PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
set_target_properties(StationSystems PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(StationSystems PRIVATE /W3 /permissive-)
else()
    target_compile_options(StationSystems PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(STATION_SYSTEMS_TESTS_DEFAULT ${PROJECT_IS_TOP_LEVEL})
if(BUILD_TESTING)
    set(STATION_SYSTEMS_TESTS_DEFAULT ON)
endif()
option(STATION_SYSTEMS_BUILD_TESTS "Build public Station consumer and scenario tests" ${STATION_SYSTEMS_TESTS_DEFAULT})
if(STATION_SYSTEMS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
