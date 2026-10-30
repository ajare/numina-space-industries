cmake_minimum_required(VERSION 3.24)
project(NSICore VERSION 0.1.0 LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endif()

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

add_library(nsi-core STATIC src/Installation.cpp src/ThermalPresets.cpp src/WorldPresets.cpp)
add_library(nsi::core ALIAS nsi-core)
target_compile_features(nsi-core PUBLIC cxx_std_20)
target_link_libraries(nsi-core PUBLIC Promethium::Serialization)
set_target_properties(nsi-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(nsi-core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

if(BUILD_TESTING)
    add_executable(nsi-core-thermal-presets tests/ThermalPresets.cpp)
    target_link_libraries(nsi-core-thermal-presets PRIVATE nsi::core)
    add_test(NAME nsi-core-thermal-presets COMMAND nsi-core-thermal-presets)
    set_tests_properties(nsi-core-thermal-presets PROPERTIES
        LABELS "core;functional;validation-fast" TIMEOUT 30)

    add_executable(nsi-core-doors tests/Doors.cpp)
    target_link_libraries(nsi-core-doors PRIVATE nsi::core)
    add_test(NAME nsi-core-doors COMMAND nsi-core-doors)
    set_tests_properties(nsi-core-doors PROPERTIES
        LABELS "core;persistence;functional;validation-fast" TIMEOUT 30)

    add_executable(nsi-core-serialization tests/InstallationSerialization.cpp)
    target_link_libraries(nsi-core-serialization PRIVATE nsi::core)
    add_test(NAME nsi-core-serialization COMMAND nsi-core-serialization)
    set_tests_properties(nsi-core-serialization PROPERTIES
        LABELS "core;persistence;functional;validation-fast" TIMEOUT 30)

    add_executable(nsi-core-world-presets tests/WorldPresets.cpp)
    target_link_libraries(nsi-core-world-presets PRIVATE nsi::core)
    add_test(NAME nsi-core-world-presets COMMAND nsi-core-world-presets)
    set_tests_properties(nsi-core-world-presets PROPERTIES
        LABELS "core;functional;validation-fast" TIMEOUT 30)

    add_executable(nsi-core-consumer tests/CoreConsumer.cpp)
    target_link_libraries(nsi-core-consumer PRIVATE nsi::core)
    add_test(NAME nsi-core-consumer COMMAND nsi-core-consumer)
    set_tests_properties(nsi-core-consumer PROPERTIES
        LABELS "core;functional;validation-fast" TIMEOUT 30)
endif()
