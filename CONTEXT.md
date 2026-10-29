# Numina Space Industries

Installation geometry, station systems simulation, and the Editor used to author installations.

## Language

**Installation**:
A structure consisting of one or more Levels. Its geometry is distinct from the systems simulated aboard it.
_Avoid_: Space Station (for the geometry model)

**Level**:
A rectangular grid of Cells at one vertical position in an Installation. Levels may have different dimensions and horizontal offsets; consecutive Levels are 3 m apart vertically.
_Avoid_: Deck

**Cell**:
One position in a Level's grid with a 2 m × 2 m footprint, belonging to exactly one Sector and carrying floor, wiring, and piping layers. One Cell across one Level's height represents 12 m³.

**Sector**:
A connected region of same-type Cells joined across absent Walls on one Level. It has a name, an area measured in Cells, and a height.

**Interior**:
The Sector type indicating that floor exists at its Cells. Its height claims the space above that footprint across consecutive existing Levels, where overlapping floors or other Interior volumes are forbidden.
_Avoid_: Filled

**Void**:
The Sector type indicating empty space with no floor, distinct from the Void atmosphere preset, which means no atmosphere compartment. Void Sectors claim no volume and have height one.

**Atmosphere preset**:
A Sector's starting atmosphere condition, independent of its size: Earth-like specifies gas composition, pressure, temperature and humidity, while Void means no atmosphere compartment, not a zero-pressure compartment. Unlike the Void Sector type, the Void atmosphere preset can describe an Interior Sector with a floor but no compartment.

**Wall**:
A shared edge between neighbouring Cells, or an edge at a Level's boundary. An ordinary present Wall is Interior between two Interior Cells and Exterior between Interior and Void or outside the grid; an absent Wall is None. A Door is a special present Wall.

**Door**:
A special Wall occupying one edge, with a clear opening 0.9 m wide and 2 m tall, supported by solid collinear Walls at both ends. Doors cannot share an endpoint and remain Sector boundaries.

**Atmosphere pathway**:
An opening through an authored Door that permits gas and its carried heat to cross between atmosphere compartments, or from a compartment to exterior space. It is distinct from a Thermal link, which conducts heat without transferring gas.

**Bulk gas flow**:
Pressure-driven movement of the upstream gas mixture through an open Atmosphere pathway. It is distinct from composition mixing, where species can exchange even at equal total pressure.

**Choked flow**:
Bulk gas flow limited by sonic conditions at an opening. Below the critical downstream/upstream absolute-pressure ratio, further lowering downstream pressure does not increase flow for unchanged upstream conditions; above that ratio the flow is subsonic.

**Discharge coefficient**:
The dimensionless reduction of an opening's ideal mass flow due to real opening losses. It is separate from geometric area and Door openness.

**Pressure**:
The absolute total gas pressure in a Sector's atmosphere, measured in kPa. A gas's partial pressure is its contribution to that total; its wet mole fraction includes water vapour in the mixture, unlike a dry-gas fraction or mass fraction.

**Hull type**:
An Installation-wide choice of exterior thermal assembly, characterised by thermal transmittance and radiative properties. The Hull is the physical skin enclosing the Installation, not the empty space outside or an individual geometric edge.
_Avoid_: Void (empty space or the no-compartment atmosphere preset), Exterior Wall (a geometric edge, not the Hull), per-Sector hull

**Partition type**:
An Installation-wide choice of internal thermal assembly for shared Walls and floors between Sectors, characterised by thermal transmittance.
_Avoid_: Wall (a geometric edge, not an assembly type), Hull type (the exterior assembly), per-Wall material

**Exterior skin**:
The surface area of a Sector facing void, comprising its sides, roof and exposed floor underside, measured in square metres. It includes surfaces facing internal voids, not just the Installation's outer perimeter.
_Avoid_: Exterior Wall (only an edge contributing to side area), Hull (the physical assembly, not an area), perimeter

**Thermal transmittance (U-value)**:
Heat flow per unit surface area per unit temperature difference through a hull or partition assembly, measured in W/(m²·K).
_Avoid_: Thermal conductivity (a material property), conductance (the whole-surface value after multiplying by area), insulation rating

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
