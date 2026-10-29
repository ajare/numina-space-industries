# Systems bridge

Source-integrated C++20 mapping from Installation geometry to Station Systems
atmosphere compartments. Add this directory with `add_subdirectory` and link
only `nsi::systemsBridge`; its public headers and dependencies are transitive.
It can also be configured standalone. Core still depends only on serialization,
not on the bridge, simulation, or Lua.

```cpp
#include <nsi/systems/Atmosphere.h>

station::StationSystemsSettings settings;
settings.atmospheres = nsi::systems::expandInstallation(installation);
for (const auto& door : nsi::systems::expandDoors(installation))
    if (door.pathway) settings.atmospherePathways.push_back(*door.pathway);
auto thermal = nsi::systems::expandThermal(installation);
settings.hull = std::move(thermal.hull);
settings.thermalLinks = std::move(thermal.links);
settings.solarCycle = std::move(thermal.solarCycle);
```

`expandSector` returns an optional compartment (invalid indices throw).
Void geometry or a Void atmosphere preset returns no compartment. Expansion
via `expandInstallation` returns only atmospheres. IDs are
`sector-<levelIndex>-<uid>`.

`expandDoors` returns one control binding per explicitly authored Door, keyed by
Level and Wall index, with an optional initially closed `AtmospherePathwaySettings`.
Pathway ids are `door-<level>-<wall>`; the clear opening is 0.9 m wide × 2 m
high (1.8 m²), smaller than the Wall it occupies and independent of edge
orientation or Sector height. The default discharge coefficient is 0.7.
The solver uses compressible subsonic/choked bulk flow with separate composition
mixing and inventory-dependent gas heat capacity on connected compartments. The runtime changes `openFraction` directly
between 0 and 1, without rebuilding compartments. Exterior/geometry-Void
endpoints map to `$exterior`; an Interior Void atmosphere preset is absence,
not vacuum, so it does not generate a pathway. Same-compartment endpoints also
have no pathway. Both cases still permit visual Door operation. Ordinary
adjacency never creates a pathway, and Door state is not written into geometry.

Earth-like is intensive: 101.325 kPa total, 20 °C, 40% RH, dry O₂ fraction
0.2095 and CO₂ fraction 0.0004. The spec's listed dry fractions total 0.9907;
because the simulation models only three dry species, the unmodelled 0.0093
balance is included in nitrogen, giving N₂ fraction 0.7901. Fractions therefore
sum to one without changing O₂ or CO₂. Water vapour consumes its share of the
total pressure **before** the dry gases are sized using the library's gas
constant and molar masses.

Every mass scales with `Sector::volumeM3()` (12 m³ per Cell per Level).
Authored starting thermal capacity uses 0.851 kJ/(K·m³), density × **cv**, not
advected **cp** (1.005 kJ/(kg·K)). Pathway-connected runtime compartments instead
calculate gas capacity and enthalpy from current species inventories; the fixed
authored values do not introduce structural heat mass (ADR-0003). Declared limits are 90–110 kPa total, minimum 16 kPa O₂,
and maximum 5 kPa CO₂.

Sector assignments pass through unchanged: occupant count and all four rates,
leak rate, reservoir id and exchanger conductance, and optional fire parameters.
A reservoir supplies heat-only coupling; an empty id leaves the compartment
adiabatic when no geometry-derived thermal link is present. Fire parameters enable the simulation's injectable `fire` fault;
they do not ignite a fire at startup. Declared limits report crossings without
clamping the integrated state.

`expandThermal` maps total exterior skin to one station-wide radiating Hull and
one drain per pressurised Sector with nonzero area. Core's `exteriorSkin` reports
`sideM2`, `roofM2`, and `floorUndersideM2` separately. Roof coverage is tested
above the entire height claim; underside coverage is tested immediately below
the floor, including tall claims from lower Levels. `interiorAtWorld` resolves
Interior volume using world Cell coordinates (Level offset plus Cell position),
independent of intermediate grid bounds. Internal Void courtyards count as
exterior just like space. Conductance is preset
U × area / 1000 in kW/K. Hull emitting area is the sum of linked areas, projected
area is one quarter, and panel heat capacity is 0.4 kJ/(K·m²) times that area.
Optical properties use the jointly calibrated core presets (ADR-0002). Solar
profile level is constant zero: absorbed power is exactly zero with positive
optical coefficients. No hull is emitted without a drain, and no hull choice
means no links. Thermal expansion never emits atmosphere pathways; only
explicitly authored Doors can create them through `expandDoors`.

When a Partition type is present, shared Interior Walls conduct between
pressurised Sectors on the same Level. Each edge contributes its length times
the smaller Sector height in metres; all shared edges (including disjoint
segments) are summed into one link per sorted uid pair. Pair link ids are
`pair-<level>-<lowerUid>-<higherUid>`: uids are Level-local, so the Level namespace
prevents collisions while preserving identity across Sector index reshuffles.
Touching height claims also conduct across floors: a lower Sector's roof at
`level + height` must meet an upper Sector's starting Level, and each overlapping
world-coordinate Cell contributes 4 m². Different grid sizes and offsets do not
clip claims at intermediate Levels. Cross-Level ids are
`pair-<lowerLevel>-<uid>-<upperLevel>-<uid>`; both interface kinds use one canonical
(Level, uid) endpoint-pair accumulator. A valid pair cannot share both a side
Wall and a floor: floor contact requires disjoint height intervals, while side
contact requires overlapping height intervals.
The same Partition U-value times summed shared area is converted to kW/K at
this boundary. A missing Partition type emits no partition links, and no Hull
suppresses all geometry-authored coupling.

The runtime consumer follows the station-systems scenario-rig precedent and
checks isolated temperatures across ticks, active loads/couplings, and pressure
and gas-quality crossings. Editing-identity coverage is tracked by #9, and
domain records by #10.
