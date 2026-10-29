# Numina Space Industries

Source-consumed C++20 Station simulation and generic serialization, clean-imported
from Promethium's `refactor/library` worktree. APIs and simulation behavior are
unchanged. See [Station Systems](station-systems/README.md) for API contracts.

## Build and test

Requires CMake 3.24+, a C/C++20 toolchain and Git. CMake uses installed yaml-cpp
when available, otherwise fetches its pinned source; Lua is fetched at a pinned
revision. First configuration therefore requires network access unless those
dependencies are cached.

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --config Release -j 16
ctest --test-dir build-linux -C Release -j 16 --output-on-failure
```

Consumers may use `add_subdirectory(path/to/numina-space-industries)` or add
`station-systems/` directly. Link `StationSystems::StationSystems`; generic
serialization consumers link `Promethium::Serialization`.

The root-level [Core](core/README.md) module provides C++20 classes in
`nsi::core`, including Installation with YAML serialization. Link `nsi::core`; it is included in the
main build and also supports standalone builds or direct `add_subdirectory(core)`.

`resources/` contains shared reference/test inputs and the Editor's Font Awesome
5 solid/regular fonts, copied with `editor/include/imgui/IconsFontAwesome5.h`
from the sibling `promethium-fermide-systems-simulation` repository. The build
copies the fonts and their license into `resources/` beside the Editor executable;
keep that directory with the executable when moving it. Fonts load relative to
the executable, not the working directory. Font licenses are recorded in
`resources/FontAwesome-LICENSE.txt`; the icon header's license is alongside it
under `editor/include/imgui/`.

## Sector systems view

In the Editor, choose **Sector (S)** mode and click a Cell to select its Sector.
A **Sector** dock tab appears beside **Level Grid**, showing the selected
Sector's authored systems as nodes with the shared Station port/link catalogue.
Expand a node's **Configuration** to inspect its authored starting values.
The separate **Simulation state** section shows the matching preview's current
temperature, pressure, humidity, gas masses, wet mole fractions and partial
pressures, plus heat/loss readouts and limit flags. Values retain their last
committed state while paused, restore on reset, and show as unavailable when no
matching preview exists (including the editing frame before a geometry reset).
The view is read-only: links cannot be created, detached or deleted. Drag nodes to rearrange
the transient view layout without changing configuration or undo history;
middle/right mouse pans the canvas and **Center view** restores its origin.

Currently Sector authoring supplies an Atmosphere preset, loads, fire opt-in and
an optional reservoir coupling. Referenced reservoirs without a definition are
marked unresolved, not fabricated. Void Sectors/presets explicitly show that no
compartment exists; no neighbour heat or gas links are inferred. The diagram is
a configuration/live-state viewer, not a second simulation or a live-state editor. The imnodes
canvas follows the sibling Flow view; its source/license and small read-only
extension are tracked in `editor/vendor/imnodes/`.

The Station Systems library needs no runtime asset installation and embeds its
own default Lua. World-specific code, documents and assets remain in Promethium.
