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
This covers names, Hull and Partition types, Cells, Walls, Sector properties,
Level operations and undo/redo, including keyboard shortcuts. Level and Sector
selection remain available for inspection; saving is still allowed. Runtime
Door toggles are the exception to locked interaction, not to locked authoring:
Shift+left-click operates existing Doors only during active playback.

The host applies `SimulationPlayback::editingAllowed()` through
`EditorSession::setEditingEnabled()`. Session commands refuse mutations while
locked, and locking finishes an in-flight stroke as one undo step. New/Open
replace the scenario and reset playback, rather than mutate the running scenario;
cancelled or failed actions preserve the existing lock and preview.

## Atmosphere and pressure inspection

Sector selection exposes Atmosphere, Thermal, Systems graph and Configuration
graph tabs. Atmosphere contains total pressure, humidity, per-species mass/wet
mole percentage/partial pressure, limit warnings and rolling-minute pressure,
relative-humidity and stacked wet-mole composition plots. Pressure/humidity
plots sit under their readouts; composition is stacked on a 0–100% axis and
leaves gaps when fractions are undefined. The Configuration graph includes immediate explicit Door pathways and
thermal links with both endpoints, Hull/exterior boundaries and live signed
outward flows. It never recursively expands the Installation or invents Gas out
or Heat out wiring. Closed Doors remain visible; absent references stay unresolved.
The Configuration graph initially zooms out to fit every node. The mouse wheel
zooms around the pointer while over the canvas; middle/right drag pans, and
Fit view reframes all nodes. These controls never modify the Installation.

The Pressure render mode uses adjustable minimum/reference/maximum anchors of
0/101.325/150 kPa by default, coloured blue/green/red. Its controls, like Thermal,
remain available during playback and are view state only. Missing atmosphere
data retains Default colours. Histories cover every compartment, preserve the
last committed data while paused and clear on reset or document replacement.

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
