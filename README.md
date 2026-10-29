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
Sector's **Configuration graph** alongside its **Thermal** and **Systems graph**
tabs. The configuration graph shows authored systems as nodes with the shared
Station port/link catalogue.
Expand a node's **Configuration** to inspect its authored starting values.
The separate **Simulation state** section shows the matching preview's current
temperature, pressure, humidity, gas masses, wet mole fractions and partial
pressures, plus heat/loss readouts and limit flags. Values retain their last
committed state while paused, restore on reset, and show as unavailable when no
matching preview exists (including the editing frame before a geometry reset).
The view is read-only: links cannot be created, detached or deleted. Drag nodes to rearrange
the transient view layout without changing configuration or undo history;
middle/right mouse pans the canvas. Scroll the mouse wheel over the canvas to
zoom around the pointer. Each Sector graph initially fits all nodes into view;
**Fit view** restores that framing without changing node placement. Zoom, node
positions and panning are transient presentation state, not saved configuration.

Currently Sector authoring supplies an Atmosphere preset, loads, fire opt-in and
an optional reservoir coupling. Referenced reservoirs without a definition are
marked unresolved, not fabricated. Void Sectors/presets explicitly show that no
compartment exists; no gas pathways are inferred from adjacency. Explicit Doors
create initially closed pathways, operated with Shift+left-click in Door mode
during active playback. The **Configuration graph** shows the selected Sector's
immediate Door pathways, opposite atmosphere/exterior endpoints, thermal links
and Hull. Closed pathways stay visible. Live state includes signed outward gas
and heat rates (last-step averages). Flow regime (**sealed / no bulk flow /
subsonic / choked**), downstream/upstream pressure ratio, bulk mass flow and
per-pathway discharge coefficient are also shown. Door links use **Hosted pathways**;
conduction uses **Thermal links**. **Gas out**/**Heat out** connections appear
only when actually authored. Unresolved references remain labelled unresolved.
Derived Hull/exterior connectors are display-only; they do not add editable
ports to the simulation. The separate **Systems graph** remains heat-only. The configuration diagram is
a configuration/live-state viewer, not a second simulation or a live-state editor. The imnodes
canvas follows the sibling Flow view; its source/license and small read-only
extension are tracked in `editor/vendor/imnodes/`.

Door flow is compressible and pressure-driven for both interior and exterior
openings, with default discharge coefficient 0.7. Gas carries upstream enthalpy;
connected compartments' heat capacities shrink/grow with gas inventory, with
Hull/partition heat kept separate. Equal-pressure composition mixing remains a
separate process. Each Door has a **0.9 m × 2 m clear opening (1.8 m²)**, not
the full Wall surface. Decompression is consequently slower than the former
6 m² opening, while retaining volume-dependent compressible flow. See
[ADR-0003](docs/adr/0003-compressible-door-gas-flow.md) for assumptions and limits.

## Installation render modes

The toolbar's **Render mode** combo switches between **Default** (the existing
floor/void view), **Thermal** (live Sector atmosphere temperatures), and
**Pressure** (live absolute total pressure in kPa). In Thermal
mode, expand **Thermal rendering** in the left panel to adjust minimum, maximum
and reference temperatures in Celsius (defaults: -273, 100 and 20). The scale
interpolates blue → green → red, with pure green at the reference temperature;
values outside the scale saturate at blue/red. Sliders keep minimum < reference
< maximum within -273 to 1000 °C. Sectors without live atmosphere data keep their
Default appearance. Heatmap selection/hover uses outlines, preserving fill colours.

In Pressure mode, **Pressure rendering** provides minimum/reference/maximum
pressure sliders (defaults **0 / 101.325 / 150 kPa**) with the same blue/green/red
interpolation and saturation. Anchors remain strictly ordered and nonnegative,
within 0–1000 kPa; missing/invalid live data retains Default colours.

Render settings are editor-only: they do not modify saved Installations or the
simulation and remain adjustable during playback.

## Sector atmosphere inspection

Choose Sector mode and select a Cell. The **Atmosphere** tab shows total and
starting pressure, humidity, O2/N2/CO2/water-vapour masses, wet mole percentages
and partial pressures, plus pressure/oxygen/CO2 warnings. Percentages include
water vapour and are undefined at zero pressure; CO is listed separately as a
hazard inventory. Missing compartments and telemetry are explicitly unavailable.

The pressure plot sits directly below Pressure; the relative-humidity plot sits
below its percentage readout. A stacked area plot beneath the composition table
shows O2/N2/CO2/water-vapour wet mole percentages on a 0–100% axis. Undefined
composition (such as zero total pressure) leaves gaps, never fabricated bands.
All plots show the last **60 simulation seconds**; pressure and humidity axes
auto-fit their data. Histories are recorded for every compartment at a
simulation-time cadence (not just the selected Sector), remain unchanged while
paused, and reset on Reset/document changes. Readouts and plots
do not edit the Installation or advance the simulation.

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
