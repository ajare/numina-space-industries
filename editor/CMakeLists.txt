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
    vendor/imgui/backends/imgui_impl_sdl2.cpp
    vendor/imgui/backends/imgui_impl_opengl3.cpp
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

# Headless playback adapter keeps simulation dependencies out of the document model.
add_library(editor-simulation STATIC src/SimulationPlayback.cpp)
target_include_directories(editor-simulation PUBLIC src)
target_link_libraries(editor-simulation PUBLIC nsi::systemsBridge)

add_executable(editor
    src/main.cpp
    src/NativeFileDialogs.cpp
    src/EditorFonts.cpp
    src/DeckGridWidget.cpp
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
    nfd
    editor::imgui
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
