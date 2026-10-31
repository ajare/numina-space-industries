# The Editor: imgui application shell and shared headless document/session.
# Geometry remains independent of Station Systems simulation.

# Headless Space Station -> Deck -> Cell model and session commands, shared
# by this application and its CTest consumer (see document/CMakeLists.txt).
add_subdirectory(document)

include(FetchContent)

# CMake 4 requires an explicit policy floor when configuring dependencies
# that retain compatibility with older CMake releases.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    # Same pinned SDL2 revision proven by the sibling Promethium application.
    GIT_TAG 5d249570393f7a37e037abf22cd6012a4cc56a71
)
FetchContent_MakeAvailable(SDL2)

# Native File Dialog Extended; use the desktop portal on Linux so GTK
# development libraries are not required and Wayland is supported.
if(UNIX AND NOT APPLE)
    set(NFD_PORTAL ON CACHE BOOL "Use XDG desktop portal" FORCE)
endif()
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG cf47e2a62ba2186d3ed9cc9fb54f6a0506970710 # v1.2.1
)
FetchContent_MakeAvailable(nfd)

# Vendored imgui (see vendor/imgui/README.md). Vendoring rather than
# FetchContent keeps this application on the exact docking-branch version the
# sibling Promethium application uses and builds without network access.
add_library(editor-imgui STATIC
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/ImGuiDatePicker.cpp
    vendor/imgui/backends/imgui_impl_sdl2.cpp
    vendor/imgui/backends/imgui_impl_opengl3.cpp
)
# The date picker's selectable year range, kept equal to the range core
# validates against (nsi::core::kMinAuthoredYear / kMaxAuthoredYear). A
# static_assert in OrbitClockPanel.cpp turns any drift between the two into
# a compile error rather than a picker that offers dates the document will
# refuse.
target_compile_definitions(editor-imgui PUBLIC
    IMGUI_DATEPICKER_YEAR_MIN=1970
    IMGUI_DATEPICKER_YEAR_MAX=2200
)
add_library(editor::imgui ALIAS editor-imgui)
target_include_directories(editor-imgui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends"
)
target_compile_features(editor-imgui PUBLIC cxx_std_20)
target_link_libraries(editor-imgui PUBLIC SDL2::SDL2-static)
# Vendored sources keep their own warning profile; project warnings never
# apply to them.
if(MSVC)
    target_compile_options(editor-imgui PRIVATE /w)
else()
    target_compile_options(editor-imgui PRIVATE -w)
endif()

# Same node canvas as the sibling Station Flow view, locally vendored and
# restricted to read-only display by SectorSystemsView.
add_library(editor-imnodes STATIC vendor/imnodes/imnodes.cpp)
target_include_directories(editor-imnodes PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imnodes")
target_link_libraries(editor-imnodes PUBLIC editor::imgui)
if(MSVC)
    target_compile_options(editor-imnodes PRIVATE /w)
else()
    target_compile_options(editor-imnodes PRIVATE -w)
endif()
add_library(editor-sector-systems STATIC src/SectorSystemsGraph.cpp src/SectorSystemsView.cpp)
target_include_directories(editor-sector-systems PUBLIC src)
target_link_libraries(editor-sector-systems PUBLIC nsi::systemsBridge editor::imgui PRIVATE editor-imnodes)

# ImPlot v0.16 supports the Editor's pinned Dear ImGui 1.89.5.
FetchContent_Declare(implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG 18c72431f8265e2b0b5378a3a73d8a883b2175ff # v0.16
)
FetchContent_MakeAvailable(implot)
add_library(editor-implot STATIC
    "${implot_SOURCE_DIR}/implot.cpp"
    "${implot_SOURCE_DIR}/implot_items.cpp"
)
add_library(editor::implot ALIAS editor-implot)
target_include_directories(editor-implot PUBLIC "${implot_SOURCE_DIR}")
target_link_libraries(editor-implot PUBLIC editor::imgui)
if(MSVC)
    target_compile_options(editor-implot PRIVATE /w)
else()
    target_compile_options(editor-implot PRIVATE -w)
endif()

# Headless playback adapter keeps simulation dependencies out of the document model.
add_library(editor-simulation STATIC
    src/SimulationPlayback.cpp src/StructuralView.cpp src/CellPlaybackRuntime.cpp src/SectorThermal.cpp src/SectorAtmosphere.cpp src/InstallationRenderMode.cpp)
target_include_directories(editor-simulation PUBLIC src)
target_link_libraries(editor-simulation PUBLIC nsi::systemsBridge)

add_executable(editor
    src/main.cpp
    src/NativeFileDialogs.cpp
    src/EditorFonts.cpp
    src/DeckGridWidget.cpp
    src/InstallationRenderControls.cpp
    src/SolarArrayControls.cpp
    src/GasInjectorControls.cpp
    src/StartDatePicker.cpp
    src/OrbitDiagram.cpp
    src/OrbitSummary.cpp
    src/StellarExposureTrace.cpp
    src/SectorThermalPanel.cpp
    src/StructuralPanel.cpp
    src/SectorAtmospherePanel.cpp
    src/SimulationTimingPanel.cpp
)
target_compile_features(editor PRIVATE cxx_std_20)
target_include_directories(editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Binaries land under bin/<platform>/<config>, matching the sibling
# Promethium application's output layout.
set_target_properties(editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/x64/$<CONFIG>")
if(MSVC)
    target_compile_options(editor PRIVATE /W3 /permissive-)
else()
    target_compile_options(editor PRIVATE -Wall -Wextra -Wpedantic)
endif()
find_package(OpenGL REQUIRED)
target_link_libraries(editor PRIVATE
    editor::document
    editor-simulation
    editor-sector-systems
    nfd
    editor::implot
    SDL2::SDL2-static
    OpenGL::GL
)
# Keep runtime fonts beside the executable, independent of the launch directory.
# A target (rather than POST_BUILD) also picks up font-only changes.
set(editor_resource_directory "${CMAKE_BINARY_DIR}/bin/x64/$<CONFIG>/resources")
add_custom_target(editor-font-assets
    COMMAND ${CMAKE_COMMAND} -E make_directory "${editor_resource_directory}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/resources/fa-solid-900.ttf"
        "${PROJECT_SOURCE_DIR}/resources/fa-regular-400.ttf"
        "${PROJECT_SOURCE_DIR}/resources/FontAwesome-LICENSE.txt"
        "${editor_resource_directory}"
    VERBATIM
)
add_dependencies(editor editor-font-assets)

if(BUILD_TESTING)
    add_executable(editor-structural-view-test tests/StructuralViewTest.cpp src/StructuralPanel.cpp src/StellarExposureTrace.cpp)
    target_link_libraries(editor-structural-view-test PRIVATE editor-simulation editor::implot)
    add_test(NAME editor-structural-view COMMAND editor-structural-view-test)
    set_tests_properties(editor-structural-view PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-solar-array-controls-test tests/SolarArrayControlsTest.cpp src/SolarArrayControls.cpp)
    target_link_libraries(editor-solar-array-controls-test PRIVATE
        editor-simulation editor::document editor::implot)
    add_test(NAME editor-solar-array-controls COMMAND editor-solar-array-controls-test)
    set_tests_properties(editor-solar-array-controls PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-start-date-picker-test tests/StartDatePickerTest.cpp src/StartDatePicker.cpp)
    target_link_libraries(editor-start-date-picker-test PRIVATE
        editor-simulation editor::document editor::implot)
    add_test(NAME editor-start-date-picker COMMAND editor-start-date-picker-test)
    set_tests_properties(editor-start-date-picker PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-orbit-diagram-test tests/OrbitDiagramTest.cpp src/OrbitDiagram.cpp
        src/OrbitSummary.cpp src/StellarExposureTrace.cpp src/StructuralPanel.cpp)
    target_link_libraries(editor-orbit-diagram-test PRIVATE
        editor-simulation editor::document editor::implot)
    add_test(NAME editor-orbit-diagram COMMAND editor-orbit-diagram-test)
    set_tests_properties(editor-orbit-diagram PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-orbit-summary-test
        tests/OrbitSummaryTest.cpp src/OrbitSummary.cpp src/StellarExposureTrace.cpp src/StructuralPanel.cpp)
    target_link_libraries(editor-orbit-summary-test PRIVATE
        editor-simulation editor::document editor::implot)
    add_test(NAME editor-orbit-summary COMMAND editor-orbit-summary-test)
    set_tests_properties(editor-orbit-summary PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 60)

    add_executable(editor-sector-node-view-test tests/SectorSystemsTest.cpp)
    target_link_libraries(editor-sector-node-view-test PRIVATE editor-sector-systems editor-simulation editor::document)
    add_test(NAME editor-sector-node-view COMMAND editor-sector-node-view-test)
    set_tests_properties(editor-sector-node-view PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-installation-render-mode-test
        tests/InstallationRenderModeTest.cpp src/DeckGridWidget.cpp src/InstallationRenderControls.cpp src/GasInjectorControls.cpp)
    target_link_libraries(editor-installation-render-mode-test PRIVATE
        editor-simulation editor::document editor::imgui)
    add_test(NAME editor-installation-render-mode COMMAND editor-installation-render-mode-test)
    set_tests_properties(editor-installation-render-mode PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-sector-thermal-panel-test
        tests/SectorThermalPanelTest.cpp src/SectorThermalPanel.cpp src/SectorAtmospherePanel.cpp src/SimulationTimingPanel.cpp)
    target_link_libraries(editor-sector-thermal-panel-test PRIVATE editor-simulation editor::implot)
    add_test(NAME editor-sector-thermal-panel COMMAND editor-sector-thermal-panel-test)
    set_tests_properties(editor-sector-thermal-panel PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-sector-systems-test tests/SectorThermalTest.cpp)
    target_link_libraries(editor-sector-systems-test PRIVATE editor-simulation)
    add_test(NAME editor-sector-systems COMMAND editor-sector-systems-test)
    set_tests_properties(editor-sector-systems PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-structural-cost-benchmark
        tests/StructuralCostBenchmark.cpp src/SectorThermalPanel.cpp src/SectorAtmospherePanel.cpp src/SimulationTimingPanel.cpp)
    target_include_directories(editor-structural-cost-benchmark PRIVATE "${PROJECT_SOURCE_DIR}/systems-bridge/tests")
    target_link_libraries(editor-structural-cost-benchmark PRIVATE editor-simulation editor::implot)
    add_executable(editor-reset-expansion-test tests/ResetExpansionTest.cpp)
    target_link_libraries(editor-reset-expansion-test PRIVATE editor-simulation)
    add_test(NAME editor-reset-expansion COMMAND editor-reset-expansion-test)
    set_tests_properties(editor-reset-expansion PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    # malloc_usable_size accounting is deliberately Linux-only. This target is
    # evidence tooling, not part of the portable numerical/public API.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(editor-integrated-thermal-benchmark
            tests/IntegratedThermalBenchmark.cpp src/StructuralPanel.cpp src/StellarExposureTrace.cpp
            src/SectorThermalPanel.cpp src/SectorAtmospherePanel.cpp src/SimulationTimingPanel.cpp)
        target_include_directories(editor-integrated-thermal-benchmark PRIVATE "${PROJECT_SOURCE_DIR}/systems-bridge/tests")
        target_link_libraries(editor-integrated-thermal-benchmark PRIVATE editor-simulation editor::implot)
        add_test(NAME editor-integrated-thermal-smoke COMMAND editor-integrated-thermal-benchmark 1 8)
        set_tests_properties(editor-integrated-thermal-smoke PROPERTIES
            LABELS "editor;functional;validation-fast" TIMEOUT 120)

        # Serial stellar-exposure cost gate evidence tooling, same Linux-only
        # allocation accounting as the integrated gate above. The full matrix
        # is run by hand for the report; this smoke row keeps the tool honest.
        add_executable(editor-stellar-cost-benchmark
            tests/StellarCostBenchmark.cpp src/StructuralPanel.cpp src/StellarExposureTrace.cpp
            src/SectorThermalPanel.cpp src/SectorAtmospherePanel.cpp src/SimulationTimingPanel.cpp)
        target_include_directories(editor-stellar-cost-benchmark PRIVATE "${PROJECT_SOURCE_DIR}/systems-bridge/tests")
        target_link_libraries(editor-stellar-cost-benchmark PRIVATE editor-simulation editor::implot)
        add_test(NAME editor-stellar-cost-smoke COMMAND editor-stellar-cost-benchmark 1 8)
        set_tests_properties(editor-stellar-cost-smoke PROPERTIES
            LABELS "editor;functional;validation-fast" TIMEOUT 120)
    endif()
    add_executable(editor-cell-playback-benchmark tests/CellPlaybackBenchmark.cpp)
    target_link_libraries(editor-cell-playback-benchmark PRIVATE editor-simulation)
    add_executable(editor-simulation-test tests/SimulationPlaybackTest.cpp)
    target_link_libraries(editor-simulation-test PRIVATE editor-simulation editor::document)
    add_test(NAME editor-simulation-playback COMMAND editor-simulation-test)
    set_tests_properties(editor-simulation-playback PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-events-test tests/EditorEventsTest.cpp)
    target_compile_features(editor-events-test PRIVATE cxx_std_20)
    target_include_directories(editor-events-test PRIVATE src)
    target_link_libraries(editor-events-test PRIVATE SDL2::SDL2-static)
    add_test(NAME editor-event-responsiveness COMMAND editor-events-test)
    set_tests_properties(editor-event-responsiveness PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 10)

    add_executable(editor-overview-test tests/OverviewGeometryTest.cpp src/OverviewGeometry.cpp)
    target_include_directories(editor-overview-test PRIVATE src)
    target_link_libraries(editor-overview-test PRIVATE editor::document)
    add_test(NAME editor-overview-geometry COMMAND editor-overview-test)
    set_tests_properties(editor-overview-geometry PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-overview-render-test
        tests/OverviewRenderTest.cpp src/OverviewRenderer.cpp src/OverviewGeometry.cpp)
    target_compile_features(editor-overview-render-test PRIVATE cxx_std_20)
    target_include_directories(editor-overview-render-test PRIVATE src)
    target_compile_definitions(editor-overview-render-test PRIVATE
        EDITOR_MPP_LOG_DIR="${CMAKE_BINARY_DIR}/logs/overview-test/$<CONFIG>")
    target_link_libraries(editor-overview-render-test PRIVATE
        editor::document SDL2::SDL2-static ext::mpp ext::mpp-mesh ext::mpp-helper ext::mpp-program)
    add_test(NAME editor-overview-render COMMAND editor-overview-render-test)
    set_tests_properties(editor-overview-render PROPERTIES
        LABELS "editor;gui;functional;validation-fast" TIMEOUT 30 SKIP_RETURN_CODE 77)

    add_executable(editor-fonts-test tests/EditorFontsTest.cpp src/EditorFonts.cpp)
    target_include_directories(editor-fonts-test PRIVATE include src)
    target_link_libraries(editor-fonts-test PRIVATE editor::imgui)
    add_dependencies(editor-fonts-test editor-font-assets)
    add_test(NAME editor-fonts COMMAND editor-fonts-test "${editor_resource_directory}")
    set_tests_properties(editor-fonts PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)
endif()

if(WIN32)
    target_link_libraries(editor PRIVATE SDL2::SDL2main)
    set_target_properties(editor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
