include(Scenarios.cmake)

# Compile every public header in isolation, with only the supported target's
# transitive requirements. Catch missing includes and hidden parent dependencies.
file(GLOB_RECURSE station_public_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/station/*.h")
set(station_header_checks)
foreach(header IN LISTS station_public_headers)
    file(RELATIVE_PATH include_name "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${header}")
    string(MAKE_C_IDENTIFIER "${include_name}" check_name)
    set(check_source "${CMAKE_CURRENT_BINARY_DIR}/headers/${check_name}.cpp")
    file(GENERATE OUTPUT "${check_source}" CONTENT "#include <${include_name}>\n")
    list(APPEND station_header_checks "${check_source}")
endforeach()
add_library(station-public-header-checks OBJECT ${station_header_checks})
target_link_libraries(station-public-header-checks PRIVATE StationSystems::StationSystems)

add_executable(station-step-timing-consumer StepTimingConsumer.cpp)
target_link_libraries(station-step-timing-consumer PRIVATE StationSystems::StationSystems)
add_test(NAME station-library-step-timing COMMAND station-step-timing-consumer)
set_tests_properties(station-library-step-timing PROPERTIES LABELS "station;functional;validation-fast")

add_executable(station-reporting-consumer ReportingConsumer.cpp)
# Public library target only: no core, editor, or private include paths.
target_link_libraries(station-reporting-consumer PRIVATE StationSystems::StationSystems)
target_compile_definitions(station-reporting-consumer PRIVATE
    STATION_FIXTURE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_test(NAME station-library-reporting COMMAND station-reporting-consumer)
set_tests_properties(station-library-reporting PROPERTIES
    LABELS "station;functional;validation-fast" TIMEOUT 120)

add_executable(station-command-consumer CommandConsumer.cpp)
target_link_libraries(station-command-consumer PRIVATE StationSystems::StationSystems)
add_test(NAME station-library-commands COMMAND station-command-consumer)
set_tests_properties(station-library-commands PROPERTIES
    LABELS "station;functional;persistence;validation-fast" TIMEOUT 120)

add_executable(station-runtime-consumer RuntimeConsumer.cpp)
target_link_libraries(station-runtime-consumer PRIVATE StationSystems::StationSystems)
add_test(NAME station-library-runtime COMMAND station-runtime-consumer)
# Prove the embedded default is usable with no assets in the working directory.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asset-free")
set_tests_properties(station-library-runtime PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asset-free"
    LABELS "station;functional;validation-fast" TIMEOUT 60)

add_executable(station-persistence-consumer PersistenceConsumer.cpp)
target_link_libraries(station-persistence-consumer PRIVATE
    StationSystems::StationSystems Promethium::Serialization)
add_test(NAME station-library-persistence COMMAND station-persistence-consumer)
set_tests_properties(station-library-persistence PROPERTIES
    LABELS "station;persistence;functional;validation-fast" TIMEOUT 30)

add_executable(station-authoring-consumer AuthoringConsumer.cpp)
target_link_libraries(station-authoring-consumer PRIVATE StationSystems::StationSystems)
target_compile_definitions(station-authoring-consumer PRIVATE
    STATION_FIXTURE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_test(NAME station-library-authoring COMMAND station-authoring-consumer)
set_tests_properties(station-library-authoring PROPERTIES
    LABELS "station;functional;validation-fast" TIMEOUT 30)

add_executable(station-configuration-consumer ConfigurationConsumer.cpp)
# The standalone build-and-test entry point builds all public API consumers.
add_dependencies(station-configuration-consumer station-public-header-checks station-step-timing-consumer station-scenario-consumer station-command-consumer station-persistence-consumer station-authoring-consumer station-runtime-consumer station-reporting-consumer)
if(TARGET nsi-serialization-consumer)
    add_dependencies(station-configuration-consumer nsi-serialization-consumer)
endif()
# Deliberately only the public target: no parent includes, helpers or core.
target_link_libraries(station-configuration-consumer PRIVATE StationSystems::StationSystems)
add_test(NAME station-library-configuration COMMAND station-configuration-consumer)
set_tests_properties(station-library-configuration PROPERTIES
    LABELS "station;functional;validation-fast" TIMEOUT 30)
