# Installation editing

The Editor directly authors `nsi::core::Installation`; it has no separate geometry
model. The old `SpaceStation`, `Deck` and `CellState` headers provide source aliases
only, so existing overview integrations can consume the same core document.
They do not provide support for old document formats.

## Authoring

The toolbar ComboBox or plain C/W/S/D keys select Cell/Wall/Sector/Door mode.
Shortcuts are disabled while entering text, using modifiers, or interacting
with a popup. Switching mode finishes any current paint stroke.

Modes:

- **Cell**: click/drag to paint Interior or Void. A stroke paints the opposite of
  its starting Cell's type, never toggling revisited Cells, and is one undo step.
  Painting creates Exterior Walls along the changing floor boundary, removes
  obsolete Exterior Walls, and preserves still-valid authored Interior Walls.
  Automatic Wall changes are included in the stroke's undo/redo.
- **Wall**: highlight the nearest shared edge, then click/drag to paint Walls.
  Starting on an absent Wall adds Walls; starting on a present Wall erases them.
  Each visited edge uses Interior/Exterior classification from its adjacent Cells,
  and revisiting an edge never toggles it again. Sectors rebuild live and the
  whole drag is one undo step. Ineligible Void/Void edges are skipped.
- **Door**: left-click the nearest edge to place a Door; right-click clears the
  edge to None. The clear opening is 0.9 m wide × 2 m high (1.8 m²), not the full
  Wall. The cyan Door symbol occupies the centre 0.9 m of the edge, with solid
  wall at both sides; the open gap has that same width. Doors have green/red
  placement previews. Each click
  is one undo step. A Door needs solid collinear Walls at both ends and cannot
  share an endpoint with another Door. Edits removing its supports are rejected;
  remove the Door first. Doors remain Sector boundaries and retain wall thermal
  coupling. While playback is actively running, Shift+left-click directly on a
  Door toggles it open/closed instantly; paused playback refuses toggles. Doors
  start closed, open Doors show a gap, and Reset/New/Open restore them closed.
  These controls affect only runtime state, never geometry, undo history or
  saved files. Valid Door endpoints exchange gas while open; exterior Doors
  vent to space. Same-compartment or missing Interior atmosphere endpoints
  still allow Door operation but have no gas pathway.
- **Sector**: click a Cell to select its Sector. The left collapsible inspector
  edits its name and Interior height and displays type, cached area and Level index.

The left inspector also edits the Installation name, Hull type directly beneath
it (including an explicit No hull option), and selected Level offset. Hull edits
route through `EditorSession` and support undo/redo. It
appends Void Levels of chosen dimensions/offsets, and removes the top Level.
Removal clears rather than deletes a Level needed by a lower Sector's height.
File → New offers width/depth, initially 16×16. The grid follows the selected Level. Invalid occupancy edits report an explanation without modifying
geometry. A rejected Cell or Wall stroke rolls back the whole stroke.

The headless `EditorSession` owns the core document, saved baseline and snapshot
undo/redo history. Snapshots restore automatically changed Walls and Sector
metadata exactly; an inverse Cell edit would not suffice. New/Open reset history;
saving preserves it. Rendering, native dialogs and file I/O remain adapters.

## Simulation edit lock

Installation editing is disabled while playback is playing (even at time zero)
or elapsed simulation time is non-zero (even when paused). Reset returns to the
start and re-enables editing. The inspector explains why editing is unavailable.
This covers names, Hull and Partition types, the solar-array enabled choice, Cells, Walls, Sector properties,
Level operations and undo/redo, including keyboard shortcuts. Level and Sector
selection remain available for inspection; saving is still allowed. Runtime
Door toggles are the exception to locked interaction, not to locked authoring:
Shift+left-click operates existing Doors only during active playback.

The host applies `SimulationPlayback::editingAllowed()` through
`EditorSession::setEditingEnabled()`. Session commands refuse mutations while
locked, and locking finishes an in-flight stroke as one undo step. New/Open
replace the scenario and reset playback, rather than mutate the running scenario;
cancelled or failed actions preserve the existing lock and preview.

The inspector's **Solar array enabled** checkbox writes the Installation's
`solarArrayEnabled` choice through `EditorSession`, including dirty tracking and
undo/redo. Missing YAML fields mean enabled; only a disabled choice is emitted,
so existing documents gain no default-only field. **Add solar array** creates an
optional `solarArray` record (60 m² blanket, triple-junction GaAs initially);
missing records remain absent on load/save. Its collapsible header contains
blanket area, cell technology, enable and removal controls, all edit-locked and
undoable. Removing the array preserves its independent enable choice.

The header reads unclipped per-array generation from a power-only systems
runtime on the consumer-less `solar` bus, never a bus dispatch total. Disabled
(including zero usable area) takes precedence, then absent Orbit means **No
star**, then the systems-side shadow state means **Eclipse** — the snapshot's
`eclipse` flag, which the systems side sets from the light alone and the header
never infers from zero output. A tracking failure pinned at zero availability
and a blanket worn to nothing both deliver nothing in full sun and are not
night. Output too small to print as a tenth reads **<0.1 kW** rather than a
rounded-away `0.0 kW`. An absent array hides the header entirely.
Missing/stale snapshots show **Unavailable**, never a measured zero. Dynamic
labels retain one stable ImGui ID so collapse state survives output changes.
Before playback the readout previews the next 0.1-second midpoint tick without
advancing playback time; during playback it reports the last committed tick.
See [ADR-0006](../../docs/adr/0006-native-playback-lifecycle-stage.md) for the
power-only exception: no second runtime owns any native gas or heat.

The left inspector's **Update time** chart stacks native fluid/thermal solver
and Lua lifecycle durations for the last **50 successful updates** (five seconds
at 10 Hz). Its horizontal axis uses update cadence, not simulated time, so changing
playback speed does not change the window. Pausing freezes the chart. Startup
shows only collected samples; Reset/document replacement clears history.

The separate solver/Lua readouts and statusbar's **Sim update (10-run avg)** use
the last ten successful updates, or available samples during startup. The total
is the sum of the two measured stages: Lua preparation/recovery and native gas,
local sources, phase and thermal processing. Construction, diagnostics, snapshot
publication, history recording and GUI work are excluded. Zero-duration calls and
failed attempts do not add samples; a cached Lua preparation on a native retry
records only that retry's actual preparation cost.

## Atmosphere and pressure inspection

Sector selection exposes Atmosphere, Thermal, Systems graph and Configuration
graph tabs. Atmosphere contains total pressure, humidity, per-species mass/wet
mole percentage/partial pressure, limit warnings and rolling-minute pressure,
relative-humidity and stacked wet-mole composition plots. Playback owns native
Cell gas/heat through `CellPlaybackRuntime`, never a second compartment solver.
Four budgeted Lua lifecycle/mirror sandboxes prepare once per host update before
physics, not inside numerical substeps. Lua/native completion is committed only
on joint success; a same-duration physical retry reuses the staged Lua step.
Script failures pause with attribution and preserve committed observations/time.
Reset clears both stages. The compartment mirror does not audit spatial Door
fluxes, and the standalone C++ plant ledger is not run alongside native transport.
Pressure histories include Cell minima/maxima; means use Cell volume, temperature
uses current heat capacity, and mole fractions use aggregate species inventory.
Warnings reflect any Cell outside its limits. Pressure/humidity
plots sit under their readouts; composition is stacked on a 0–100% axis and
leaves gaps when fractions are undefined. The Configuration graph includes immediate Door/derived breach pathways and
thermal links with both endpoints, Hull/exterior boundaries and live signed
outward flows. It never recursively expands the Installation or invents Gas out
or Heat out wiring. Closed Doors remain visible; absent references stay unresolved.
The Configuration graph initially zooms out to fit every node. The mouse wheel
zooms around the pointer while over the canvas; middle/right drag pans, and
Fit view reframes all nodes. These controls never modify the Installation.

The Pressure render mode uses adjustable minimum/reference/maximum anchors of
0/101.325/150 kPa by default, coloured blue/green/red. Its controls, like Thermal,
remain available during playback and are view state only. Missing atmosphere
data retains Default colours. Native unresolved gas is neutral grey specifically
in Thermal mode, with an explicit legend; it is not a cold temperature reading.
Pressure mode still shows its near-zero pressure. Thermal/Pressure colours are per Cell, keyed by
Level/grid index and stable Sector uid. Hover inspection remains active on the
read-only playing grid and reports composition and horizontal velocity. Optional
Airflow load arrows show drag plus pressure-gradient force on the stationary
reference object (A=1 m², V=1 m³, Cd=1), not speed. Their smooth force scale has
an adjustable half-length in N and no fixed minimum length; subpixel loads are
not drawn. Hover includes density and separate force terms. Wind diagnostics
and Object forces calculations are independent, initially enabled controls;
disabling wind reporting does not disable conservative gas momentum or forces.
Disabling forces skips the load/gradient pass and invalidates cached load queries.
These runtime options survive Reset/document replacement without adding history
or editing the Installation. Unresolved gas does not display placeholder
measurements. Loads are passive estimates, not rigid-body motion/two-way coupling.
Histories cover every compartment, preserve the
last committed data while paused and clear on reset or document replacement.
Refused native advances pause with a diagnostic, retaining the exact unadvanced
simulation request across speed changes. Bounded catch-up work retains wall-time
debt rather than silently discarding time.

## Files and unsaved changes

New, Open, Save, Save As and Exit retain the shared guarded action path. Dirty
documents offer Save/Discard/Cancel. Cancelled or failed saves block destructive
actions. Discard does not eagerly reset geometry, so a subsequent cancelled Open
still preserves the document. Successful saves alone adopt a destination/baseline.
The title's `*` and statusbar compare the whole Installation to that baseline.

Files use the [core version-4 YAML schema](../../core/README.md). Old deck-row and
name-only documents are rejected entirely. The core validates loads transactionally;
`YamlDocumentFiles` uses the shared `Promethium::Serialization` transactional file
writer for saves, preserving an existing file after a partial write failure.

Linux native dialogs use the XDG desktop portal; Windows/macOS use native backends.
Release CTest consumers exercise real files, failed writes, history, topology,
vertical occupancy and picking without a desktop. Native smoke checks should also
cover all four modes, Level selection/offsets, height conflicts, New dimensions,
grid updates, undo/redo and Save/Discard/Cancel through native dialogs.
