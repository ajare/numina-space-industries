# Numina Space Industries

Station Systems and generic serialization as a standalone library, plus the
Editor application shell that will present them.

## Language

**Space Station**:
A structure represented by a stack of Decks, with at least one Deck. Its
geometry is distinct from the systems simulated aboard it.
_Avoid_: station systems

**Deck**:
A rectangular grid of Cells at one level of a Space Station. Decks may have
different dimensions and share an origin so matching cell coordinates align
vertically.

**Cell**:
One position in a Deck's grid, either Filled or Void. Filled Cells need not
form a connected region, and a Deck may consist entirely of Void Cells.

**Filled**:
The Cell state indicating that station floor exists at that position, not a
solid block of material. Rooms, walls, and equipment are separate concepts.

**Void**:
The Cell state indicating empty space with no station floor at that position.

**Editor**:
The standalone GUI application in this repository (`editor/`) that hosts
dockable panels over a shared frame (menubar, toolbar, statusbar). Not to be
confused with the Promethium editor, which lives in another repository and is
not this term.
_Avoid_: Promethium editor, station editor

**Panel**:
A dockable content window inside the Editor's dockspace; the "Editor" panel
is the first. Panels never own chrome (menubar, toolbar, statusbar) — the
host frame does.

**Editing Session**:
The headless boundary between the Editor's interface and its geometry. It
owns the active Space Station document and the user-level commands performed
on it, and is driven both by the Editor GUI and by automated tests. Rendering,
native dialogs, and file I/O stay outside the session.
