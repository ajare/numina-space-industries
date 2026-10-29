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
compartment exists. Explicit Doors create initially closed openings, operated
with Shift+left-click in Door mode during active playback. Missing Walls facing
geometric Void/outside are full-face breaches, not sealed boundaries. Interior
Void presets never act as vacuum sinks. The **Configuration graph** shows the
selected Sector's immediate Door/breach pathways, opposite atmosphere/exterior endpoints, thermal links
and Hull. Closed pathways stay visible. Live state includes signed outward gas
and heat rates (last-step averages). Pressure-based flow regime estimates
(**sealed / balanced pressures / subsonic / choked**), downstream/upstream pressure ratio, bulk mass flow and
per-pathway discharge coefficient are also shown. Door links use **Hosted pathways**;
conduction uses **Thermal links**. **Gas out**/**Heat out** connections appear
only when actually authored. Unresolved references remain labelled unresolved.
Derived Hull/exterior connectors are display-only; they do not add editable
ports to the simulation. The separate **Systems graph** remains heat-only. The configuration diagram is
a configuration/live-state viewer, not a second simulation or a live-state editor. The imnodes
canvas follows the sibling Flow view; its source/license and small read-only
extension are tracked in `editor/vendor/imnodes/`.

Editor playback runs native conservative Cell gas dynamics and Hull/partition
heat, replacing—not supplementing—the well-mixed compartment runtime. Each
requested 10 Hz update first runs the four budgeted Lua lifecycle/mirror sandboxes;
they do not run inside fluid substeps or apply a second native plant/gas ledger.
Only joint Lua/physics success commits time and completion counters. The mirror
is independent of the spatial gas state, not a CFD audit or equipment controller.
See [ADR-0006](docs/adr/0006-native-playback-lifecycle-stage.md). Each Level
has independent horizontal flow: four gas species, pressure, temperature and wind;
there is no vertical airflow or rigid-body motion. Passive aerodynamic-load
sampling is described below. Cells store 12 m³ × Sector
height, but full gas faces stay 6 m². Doors use **0.9 m × 2 m (1.8 m²)** openings
and discharge coefficient 0.7. Restricted-face HLLE flux is a coarse aperture
model, not the standalone runtime's analytic nozzle law. Mixing is numerical,
not calibrated molecular diffusion. See [ADR-0004](docs/adr/0004-cell-atmosphere-and-horizontal-momentum.md).

Loads, oxygen starvation, humidity and condensation are local; Sector loads are
distributed by Cell volume. Work-limit refusals pause with a diagnostic and roll
back the whole update. Catch-up time is retained rather than discarded; a refused
simulation duration is retained even after changing speed. Unresolved reservoir
references refuse native playback rather than silently omitting their heat.

## Installation render modes

The toolbar's **Render mode** combo switches between **Default** (the existing
floor/void view), **Thermal (gas)** (live Cell atmosphere temperatures),
**Pressure** (live absolute total pressure in kPa), and **Structure (surfaces)**
(area-weighted exposed-side temperatures, LocalStructure backend). In Thermal
mode, expand **Thermal rendering** in the left panel to adjust minimum, maximum
and reference temperatures in Celsius (defaults: -273, 100 and 20). The scale
interpolates blue → green → red, with pure green at the reference temperature;
values outside the scale saturate at blue/red. Sliders keep minimum < reference
< maximum within -273 to 1000 °C. Cells without live atmosphere data keep their
Default appearance. **Grey in Thermal mode means vacuum/unresolved gas with no
available temperature**, not another cold phase. As gas escapes it can cool,
then the minute remainder can rewarm from the Hull/partitions before becoming
unresolved; that does not mean a breathable atmosphere has returned. Heatmap
selection/hover uses outlines, preserving fill colours.

In Pressure mode, **Pressure rendering** provides minimum/reference/maximum
pressure sliders (defaults **0 / 101.325 / 150 kPa**) with the same blue/green/red
interpolation and saturation. Anchors remain strictly ordered and nonnegative,
within 0–1000 kPa; missing/invalid live data retains Default colours.

Hover a Cell to inspect pressure, temperature, humidity, wet composition and
horizontal velocity, including while playing. **Airflow load arrows** show the
estimated push on a stationary reference object (area 1 m², volume 1 m³, Cd 1),
not gas speed. Length scales smoothly as `force / (force + scale)`; adjust
**Half-length load (N)** to change sensitivity. There is no fixed minimum arrow
length; subpixel arrows are omitted. As a room evacuates, density-dependent load
shrinks even if velocity stays similar. Hover shows density, dynamic pressure,
separate drag/pressure forces and the resultant in newtons. Unresolved or
unrepresentable loads are unavailable, never fabricated zero measurements.

**Wind diagnostics** and **Object forces** are independent calculation toggles,
both on by default. They work while paused/playing, survive Reset and document
replacement, and do not edit the Installation or advance time. Wind-off hides
optional speed/vector diagnostics, not the gas solver's mandatory momentum.
Force-on still uses solver velocity when wind diagnostics are off; force-off
skips load/gradient calculation and clears old observations.

`station/CellAerodynamics.h` provides object-specific relative-velocity drag and
coarse pressure-gradient force sampling. `CellPlaybackRuntime::objectForce`
accepts a Level/Cell and object area, volume, Cd and velocity. These are one-way
engineering estimates: no object motion, obstruction, friction, collision or
opposite gas booking is implied. See [ADR-0005](docs/adr/0005-passive-aerodynamic-loads.md).

Render settings are editor-only: they do not modify saved Installations or the
simulation and remain adjustable during playback.

## Sector atmosphere inspection

Choose Sector mode and select a Cell. The **Atmosphere** tab shows total and
starting pressure, Cell pressure minimum/maximum, humidity, O2/N2/CO2/water-vapour masses, wet mole percentages
and partial pressures, plus pressure/oxygen/CO2 warnings. Percentages include
water vapour and are undefined at zero pressure; CO is listed separately as a
hazard inventory. Pressure/partial pressures/RH are volume-weighted Cell means;
temperature is heat-capacity weighted, and composition uses total mole inventory,
not mean partial-pressure ratios. Limits warn if any Cell is out of range.
Missing compartments and telemetry are explicitly unavailable.

The pressure plot shows mean/minimum/maximum directly below Pressure; the relative-humidity plot sits
below its percentage readout. A stacked area plot beneath the composition table
shows O2/N2/CO2/water-vapour wet mole percentages on a 0–100% axis. Undefined
composition (such as zero total pressure) leaves gaps, never fabricated bands.
All plots show the last **60 simulation seconds**; pressure and humidity axes
auto-fit their data. Histories are recorded for every compartment at a
simulation-time cadence (not just the selected Sector), remain unchanged while
paused, and reset on Reset/document changes. Readouts and plots
do not edit the Installation or advance the simulation.

## Sector thermal inspection

**LocalStructure is the only native thermal backend**; there is no backend selector.
**Structure (surfaces)** keeps solid temperatures separate from air, including
warm surfaces in rooms without resolved gas. Select a Cell in Sector mode to
inspect directional surface means/min/max, capacity/energy, heat-path ledgers,
a bounded one-hop real-topology graph and simulation-time surface histories.
See [Structural Editor observations](docs/structural-editor.md) for aggregation,
diffuse well-mixed cavity/binary Door approximations, unavailable gaps and
lifecycle semantics.

The **Sector / Thermal** tab reports gas thermal state and exterior skin areas;
local solid temperatures and real thermal edges live in **Structure**. Legacy
shared-Hull/direct-gas links, C/G cooldown indicators and their Systems graph
are removed. Reset reconstructs the initial gas and solid state. The measured
performance target is still unmet; accelerated updates may explicitly refuse
without dropping time or changing fidelity. Standalone compartment APIs and
Installation YAML remain unchanged.

The Station Systems library needs no runtime asset installation and embeds its
own default Lua. World-specific code, documents and assets remain in Promethium.
