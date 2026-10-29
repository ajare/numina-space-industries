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

## Sector thermal inspection

In the Editor, choose Sector mode and select a Cell. The **Sector** panel's
**Thermal** tab shows side, roof and exposed floor-underside skin areas,
conductance and signed heat flow for every attached neighbour/Hull link.
The **Systems graph** tab draws those connections; arrows follow the last
simulation step's heat flow. Inspection remains available during playback.

Timescales use the selected compartment's geometry-derived air capacity:
`diffusion = C / G_neighbour`, `drain = C / G_hull`, in seconds. Each neighbour's
ratio is `diffusion / drain`. These are local response times with the other
endpoint held fixed, not two-body equilibration or nonlinear radiative cooling
predictions. No Hull link means an infinite drain time (ratio zero for existing
neighbour links); no links means thermally isolated. Void atmosphere presets
have no compartment and no connections. Flow is unavailable without a runtime,
and zero until the first simulation step.

The Station Systems library needs no runtime asset installation and embeds its
own default Lua. World-specific code, documents and assets remain in Promethium.
