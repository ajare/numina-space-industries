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

**Cell atmosphere**:
The gas inventory and local condition within one Interior Cell's volume, including the storage volume contributed by its Sector's height.

**Wind**:
The local horizontal velocity of the gas in a Cell. It is distinct from the movement of objects or people.

**Aerodynamic load**:
The estimated push of the local atmosphere on an object, combining drag from relative gas motion with the surrounding pressure gradient. It depends on gas density and the object's exposed area, displaced volume, shape and velocity; it is distinct from Wind speed.

**Reference object**:
The stationary object used to compare atmospheric loads between Cells, with a projected area of 1 m², displaced volume of 1 m³ and drag coefficient of one. It is a probe, not a placed object or an obstruction to airflow.

**Breach**:
An absent Wall between an Interior Cell and geometric Void or the outside of its Level's grid, exposing a full horizontal Cell face. It is distinct from a Door and from the absence of a compartment under the Void atmosphere preset.

**Atmosphere pathway**:
An opening that permits gas and its carried energy to cross between atmosphere compartments, or from a compartment to exterior space. It is distinct from a Thermal link, which conducts heat without transferring gas.

**Bulk gas flow**:
Movement of the gas mixture with its shared velocity, driven by pressure differences and persisting through inertia. It is distinct from composition mixing, where species can exchange even at equal total pressure.

**Choked flow**:
Bulk gas flow limited by sonic conditions at an opening. Below the critical downstream/upstream absolute-pressure ratio, further lowering downstream pressure does not increase flow for unchanged upstream conditions; above that ratio the flow is subsonic.

**Discharge coefficient**:
The dimensionless reduction of an opening's ideal mass flow due to real opening losses. It is separate from geometric area and Door openness.

**Pressure**:
The absolute total gas pressure at a location or across an atmosphere aggregate, measured in kPa. A gas's partial pressure is its contribution to that total; its wet mole fraction includes water vapour in the mixture, unlike a dry-gas fraction or mass fraction.

**Hull type**:
An Installation-wide choice of exterior thermal assembly, characterised by thermal transmittance and radiative properties. The Hull is the physical skin enclosing the Installation, not the empty space outside or an individual geometric edge.
_Avoid_: Void (empty space or the no-compartment atmosphere preset), Exterior Wall (a geometric edge, not the Hull), per-Sector hull

**Partition type**:
An Installation-wide choice of internal thermal assembly for shared Walls and floors between Sectors, characterised by thermal transmittance.
_Avoid_: Wall (a geometric edge, not an assembly type), Hull type (the exterior assembly), per-Wall material

**Structural assembly**:
A physical Hull panel, partition, shared floor/ceiling or Door leaf/frame, distinct from the gas on either side. A shared assembly has one physical mass even when it has two exposed sides.

**Structural surface**:
One exposed side of a Structural assembly, with its own local temperature, distinct from the temperature of adjacent gas.

**Radiation cavity**:
An Interior Sector's enclosure for diffuse thermal radiation, independent of whether the Sector has modelled gas. It represents a radiation field, not a material body or stored heat.

**Radiation portal**:
An opening that exchanges thermal radiation between cavities or with exterior space. It is distinct from solid conduction and its clear area is not reduced by a gas Discharge coefficient.

**Exterior skin**:
The surface area of a Sector facing void, comprising its sides, roof and exposed floor underside, measured in square metres. It includes surfaces facing internal voids, not just the Installation's outer perimeter.
_Avoid_: Exterior Wall (only an edge contributing to side area), Hull (the physical assembly, not an area), perimeter

**Orbital period**:
The time for the Installation to complete one revolution about its planet, measured in the inertial frame. It sets the period of both stellar exposure and eclipse; the planet's own rotation affects neither.
_Avoid_: Relative speed, rotation period (for the Installation's motion), orbit speed

**Nadir lock**:
The Installation's attitude convention: one face continuously faces the planet, so the Installation turns once per orbit. It makes each hull panel's solar incidence well defined from that panel's outward normal.
_Avoid_: Geostationary (an orbital condition, not an attitude), tumbling, spin-stabilised

**Geostationary**:
The special case where the Installation's Orbital period equals the planet's sidereal rotation, so it holds one position over the surface. A derived condition shown to the author, never an input.
_Avoid_: Hovering, stationary, locked orbit

**Stellar irradiance**:
Stellar power per unit area at the planet's orbital distance, measured in W/m² (1361 for the Sun at Earth). A property of the star and the orbit, never of the hull.
_Avoid_: Solar constant, luminosity, fullSunIrradianceKWPerM2 (legacy wire name that wrongly places it on the Hull)

**Stellar exposure**:
The fraction of full stellar irradiance reaching one hull surface at an instant, after eclipse and incidence. Zero means none; one means full sun at normal incidence.
_Avoid_: Sunlight level, solar intensity, insolation

**Axial tilt**:
The planet's lean toward its star, in degrees, held fixed while the Installation orbits the planet's equator - a permanent solstice. It is the latitude the star sits directly over, which is the same thing as the angle between the star and the Installation's orbital plane, and it decides whether eclipse occurs at all, how deep it is, and how much direct light the cross-track faces receive. Zero puts the star over the equator for the worst-case eclipse.
_Avoid_: Beta angle (the same geometry under its astronomy name), inclination (the orbit's tilt, not the planet's), declination (which swings with a season this model does not have)

**Eclipse**:
The interval in which the planet blocks the Installation's direct view of the star. It is a shadow, not night, and unrelated to the Void atmosphere preset.
_Avoid_: Night, shadow phase, blackout

**Planet shine**:
Stellar light reflected from the planet's disc plus the planet's own infrared, arriving at the hull indirectly. Distinct from direct stellar exposure and from the deep-space sink.
_Avoid_: Albedo (only its reflective half), back-radiation, ground glow

**Albedo**:
The fraction of incident stellar light the planet reflects. Distinct from the planet's infrared emission, which continues when albedo contributes nothing.
_Avoid_: Reflectivity, planet shine (the whole of it)

**Deep-space sink**:
The radiative background a surface sees when the planet does not occupy its view. Not a reservoir, and not what every exterior face sees.
_Avoid_: Vacuum temperature, exterior temperature

**Local noon**:
The orbit phase at which the star is directly overhead of the Installation, and the t = 0 reference; the sunlit arc is centred on it.
_Avoid_: Sunrise, orbit start

**Mission start date**:
The calendar day a run's clock begins on, authored as a day and always at midnight UTC — there is no time of day to set, and one arriving from any other path is refused rather than rounded. The clock a run shows is this instant plus simulated seconds, derived every frame and never stored, so the date on the document and the time on the screen are one number read at two moments rather than two clocks that could fall out of step. It is a calendar label and nothing to do with **Local noon**, which is an orbit phase: the two are independent axes, and a mission can start at any hour of its own date while its orbit still opens at noon. Defaults to 1st January 2050.
_Avoid_: Epoch (that is the zero the instant is measured from, not the field), start time (there is no time of day to author), launch date (nothing launches on it)

**Ground drift**:
The apparent motion of the Installation's sub-point across the planet's surface, in degrees per day. A derived readout for the author; zero means Geostationary.
_Avoid_: Relative speed (as an input), orbital drift

**World preset**:
One of the eight planets of the Sun authored as the body an Installation orbits — its stellar irradiance, Bond albedo, axial tilt and radius, applied together. The Sun itself is never a world preset: in this model the star and the orbited body are separate things, and every preset row already carries the Sun at that world's distance. **Deep space** ends the list rather than joining it: no star and no planet, which is no Orbit record at all rather than a world with zero radius.
_Avoid_: Star preset (the star is the irradiance, not the world), system preset (ambiguous with the atmosphere preset), empty world / null planet (Deep space is an absent record, not a zeroed one)

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
