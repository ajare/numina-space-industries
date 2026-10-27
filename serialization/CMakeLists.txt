cmake_minimum_required(VERSION 3.20)
project(PromethiumSerialization LANGUAGES CXX)
include(CTest)

# May be supplied by a containing project, or found independently.
if(NOT TARGET yaml-cpp::yaml-cpp AND NOT TARGET yaml-cpp)
    find_package(yaml-cpp CONFIG REQUIRED)
endif()
if(TARGET yaml-cpp::yaml-cpp)
    set(serialization_yaml_target yaml-cpp::yaml-cpp)
else()
    set(serialization_yaml_target yaml-cpp)
endif()

add_library(pf-serialization STATIC
    src/Serializer.cpp src/BinarySerializer.cpp src/YamlSerializer.cpp
    src/TransactionalFileWriter.cpp)
add_library(Promethium::Serialization ALIAS pf-serialization)
target_compile_features(pf-serialization PUBLIC cxx_std_20)
target_include_directories(pf-serialization PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(pf-serialization PUBLIC YAML_CPP_STATIC_DEFINE)
target_link_libraries(pf-serialization PUBLIC ${serialization_yaml_target})
set_target_properties(pf-serialization PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(BUILD_TESTING)
    # This consumer receives no core, World, application, or private includes.
    add_executable(pf-serialization-consumer tests/Consumer.cpp)
    target_link_libraries(pf-serialization-consumer PRIVATE Promethium::Serialization)
    add_test(NAME serialization-consumer COMMAND pf-serialization-consumer)
    set_tests_properties(serialization-consumer PROPERTIES LABELS "persistence;functional" TIMEOUT 30)
endif()

if(MSVC)
    target_compile_options(pf-serialization PRIVATE /W3 /permissive-)
else()
    target_compile_options(pf-serialization PRIVATE -Wall -Wextra -Wpedantic)
endif()
