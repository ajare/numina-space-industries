# Installation editing

The Editor directly authors `nsi::core::Installation`; it has no separate geometry
model. The old `SpaceStation`, `Deck` and `CellState` headers provide source aliases
only, so existing overview integrations can consume the same core document.
They do not provide support for old document formats.

## Authoring

The toolbar ComboBox or plain C/W/S keys select Cell/Wall/Sector mode.
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
- **Sector**: click a Cell to select its Sector. The left collapsible inspector
  edits its name and Interior height and displays type, cached area and Level index.

The left inspector also edits the Installation name and selected Level offset,
appends Void Levels of chosen dimensions/offsets, and removes the top Level.
Removal clears rather than deletes a Level needed by a lower Sector's height.
File → New offers width/depth, initially 16×16. The grid follows the selected Level. Invalid occupancy edits report an explanation without modifying
geometry. A rejected Cell or Wall stroke rolls back the whole stroke.

The headless `EditorSession` owns the core document, saved baseline and snapshot
undo/redo history. Snapshots restore automatically changed Walls and Sector
metadata exactly; an inverse Cell edit would not suffice. New/Open reset history;
saving preserves it. Rendering, native dialogs and file I/O remain adapters.

## Files and unsaved changes

New, Open, Save, Save As and Exit retain the shared guarded action path. Dirty
documents offer Save/Discard/Cancel. Cancelled or failed saves block destructive
actions. Discard does not eagerly reset geometry, so a subsequent cancelled Open
still preserves the document. Successful saves alone adopt a destination/baseline.
The title's `*` and statusbar compare the whole Installation to that baseline.

Files use the [core version-2 YAML schema](../../core/README.md). Old deck-row and
name-only documents are rejected entirely. The core validates loads transactionally;
`YamlDocumentFiles` uses the shared `Promethium::Serialization` transactional file
writer for saves, preserving an existing file after a partial write failure.

Linux native dialogs use the XDG desktop portal; Windows/macOS use native backends.
Release CTest consumers exercise real files, failed writes, history, topology,
vertical occupancy and picking without a desktop. Native smoke checks should also
cover all three modes, Level selection/offsets, height conflicts, New dimensions,
grid updates, undo/redo and Save/Discard/Cancel through native dialogs.
