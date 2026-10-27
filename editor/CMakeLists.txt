# The Editor: a standalone imgui application shell. It deliberately links
# nothing from station-systems or serialization; it is a host for future
# dockable panels.

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

add_executable(editor
    src/main.cpp
    src/OverviewRenderer.cpp
)
target_compile_features(editor PRIVATE cxx_std_20)
# MPP logger output lives in the build tree regardless of launch directory,
# mirroring the sibling Promethium application's PF_RENDER_LOG_DIR.
target_compile_definitions(editor PRIVATE
    EDITOR_MPP_LOG_DIR="${CMAKE_BINARY_DIR}/logs/$<CONFIG>")
# Binaries land under bin/<platform>/<config>, matching the sibling
# Promethium application's output layout.
set_target_properties(editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/x64/$<CONFIG>")
if(MSVC)
    target_compile_options(editor PRIVATE /W3 /permissive-)
else()
    target_compile_options(editor PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(editor PRIVATE
    editor::imgui
    SDL2::SDL2-static
    ext::mpp
    ext::mpp-mesh
    ext::mpp-helper
    ext::mpp-program
)
if(WIN32)
    target_link_libraries(editor PRIVATE SDL2::SDL2main)
    set_target_properties(editor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
