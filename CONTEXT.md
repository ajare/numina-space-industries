# Numina Space Industries

Installation geometry, station systems simulation, and the Editor used to author installations.

## Language

**Installation**:
A structure consisting of one or more Levels. Its geometry is distinct from the systems simulated aboard it.
_Avoid_: Space Station (for the geometry model)

**Level**:
A rectangular grid of Cells at one vertical position in an Installation. Levels may have different dimensions and horizontal offsets; consecutive Levels are one grid unit apart vertically.
_Avoid_: Deck

**Cell**:
One position in a Level's grid, belonging to exactly one Sector and carrying floor, wiring, and piping layers.

**Sector**:
A connected region of same-type Cells joined across absent Walls on one Level. It has a name, an area measured in Cells, and a height.

**Interior**:
The Sector type indicating that floor exists at its Cells. Its height claims the space above that footprint across consecutive existing Levels, where overlapping floors or other Interior volumes are forbidden.
_Avoid_: Filled

**Void**:
The Sector type indicating empty space with no floor. Void Sectors claim no volume and have height one.

**Wall**:
A shared edge between neighbouring Cells, or an edge at a Level's boundary. A present Wall is Interior between two Interior Cells and Exterior between Interior and Void or outside the grid; an absent Wall is None.

**Floor Layer**, **Wiring Layer**, **Piping Layer**:
The floor, electrical wiring, and piping content associated with a Cell, respectively.

**Paint Stroke**:
One continuous pointer gesture over a Level. Cell strokes paint the opposite of the starting Cell's type; Wall strokes add or erase according to whether the starting Wall is absent or present, and each whole stroke is one undo step.

**Editor**:
The standalone GUI application in this repository that hosts dockable panels over a shared frame. Not to be confused with the Promethium editor in another repository.
_Avoid_: Promethium editor, station editor

**Panel**:
A dockable content window inside the Editor's dockspace. Panels do not own the application's menubar, toolbar, or statusbar.

**Editing Session**:
The headless boundary between the Editor's interface and the Installation. It owns the active document and user-level commands; rendering, native dialogs, and file I/O stay outside it.
