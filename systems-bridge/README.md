# Systems bridge

The [native structural calibration study](../docs/thermal-calibration.md) (#24)
records provisional passive material/film candidates and the Release
`nsi-thermal-calibration` fixture. It does not change these production mappings
or the default thermal backend.

The [serial structural/cavity cost review](../docs/structural-cost-review.md)
(#25) adds a test-local P1 prototype, recorded Release measurements and the
`editor-structural-cost-benchmark` executable. Its measured cost/accuracy limits
are not approval for default rollout.

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

## Native spatial gas layer and Editor playback

`nsi/systems/CellAtmosphere.h` provides `expandCellAtmosphere` and
`CellAtmosphereTransport`. Editor playback uses this transport in Geometry mode
as its sole gas/heat runtime. The standalone transport's default `CellThermalMode::Adiabatic` keeps geometry heat disabled;
`CellThermalMode::Geometry` enables native Hull/partition conduction and radiation.
Explicit reservoir references have no definitions in Installation expansion and
refuse Geometry mode instead of silently losing their coupling. The standalone
compartment APIs above keep their existing behavior.

Expansion produces one gas Cell per modelled Interior Cell at its authored base
Level, with volume `12 m³ × Sector height`. All four faces are present; ordinary
Walls close them, missing Walls facing geometric Void/outside breach the full
6 m² face, and Doors start closed with a 1.8 m² clear aperture and coefficient
0.7. Faces never join Levels. An Interior Void preset has no Cell and cannot be
opened as a vacuum sink; same-Sector Doors retain their real adjacent Cell face.
Door actuation changes native face fractions only, never the authored document.
The aperture flux is the spatial HLLE prototype, not a second application of the
standalone compartment orifice flow.

Cell thermal contacts localize the **unchanged** `expandThermal` conductance
budgets to shared Walls, overlapping floors and exterior skin. Side areas retain
height scaling, roofs/floors retain world-coordinate coverage rules, and absent
exterior Walls do not acquire a new thermal skin area. Geometry mode solves these
contacts after gas transport and source updates, using the **current Cell gas
capacity**, never stale Sector capacity. Cross-Level heat contacts imply no gas
exchange. Empty/unresolved gas nodes detach without discarding their inventory.

The transport layer applies configured occupancy, moisture and proportional
leaks locally by Cell volume. Oxygen availability scales that Cell's CO2 and
sensible heat, not the entire Sector's metabolism. Sources straddle gas transport;
heat precedes leakage so departing gas carries its share. Condensation is local
and uses the existing latent equilibrium, including the sensible/kinetic energy
and momentum removed with condensate. Source/phase intervals are at most 0.1 s,
also at accelerated speeds. There is no ignition at startup or plant/fault solver
in this layer.

`advance(seconds, heatJ)` accepts optional **per-Cell joules for the whole call**
from an external thermal source. In Adiabatic mode, omission leaves only the
configured occupant/latent heat; in Geometry mode, these joules are **additional**
to native Hull/partition heat. Do not supply the same contact flux twice. The
caller must commit any external body's opposite energy booking only after
success. A failed acoustic/thermal budget, invalid heat input or numerical refusal
restores the entire update's gas, Hull, heat-link/source/phase ledgers, momentum
balance and clock, including all already-completed source slices. The default
whole-call budgets are 4096 acoustic steps, 4096 thermal iterations/analytical
star solves and 4096 source slices. A refusal never commits partial time.

The sparse thermal step uses passive backward Euler on the Cell/contact graph,
with exact star elimination where possible and preconditioned CG otherwise.
This is first-order temporal conduction (bounded to 0.1 s), not the compartment
solver's dense second-order extrapolation. It avoids a quadratic matrix and
maintains the stiff trace-gas limit. Shared links book one signed transfer;
node heat and link-flux residuals must meet a `1e-7 J + 1e-12 × sum(abs(node heat))`
convergence budget. A bounded energy-constraint adjustment on the largest-capacity
body closes summed heat without subtracting large opposing fluxes on trace gas.
Kelvin/passive bounds are checked, never temperature-clamped. Hull radiation runs
in half-steps around conduction using the existing nonlinear integrator, with
midpoint solar exposure. `thermal()` exposes Hull and link energy ledgers.

Sector outputs contain actual species masses, total/kinetic energy, heat-capacity-
weighted temperature and volume-weighted pressure mean/min/max. Temperature and
mean pressure are aggregated independently, not inferred by treating a nonuniform
Sector as one equilibrated compartment. Ledgers separate inventory energy,
external/occupant/latent/conducted heat, transported energy, exterior losses and retained
condensate. Global momentum includes source impulse, exterior pressure/momentum
flux and fixed-Wall reaction. `faceTransfers()` spans the complete call, not just
its final 0.1 s slice. Successful updates allocate no heap memory.

The Release benchmark includes transport, sources, condensation and aggregation
for 4096 Cells. `thermal-*` scenarios also include Hull conduction/radiation;
**GUI and Editor snapshot adaptation are excluded from this transport-only benchmark**:

```sh
cmake --build build-linux --config Release --target nsi-cell-atmosphere-benchmark -j 16
build-linux/systems-bridge/nsi-cell-atmosphere-benchmark
```

On the development Ryzen AI MAX+ 395, two warmups plus ten fresh-start trials
measured about **5.5 ms** for a wave, **7.0 ms** for four breaches, **9.7 ms** for
sustained breaches after one simulated second, and **8.0 ms** for breaches with
4096 occupants' loads, per 0.1 simulated seconds **including geometry heat**.
Exact star elimination reduces common Hull solve work; rebasing the resting
background prevents uniform sources from waking the entire gas mesh. Both retain
the same physical equations and sound speed. These are measurements, not
integrated playback or scheduling guarantees. Geometry construction is excluded.
An optional scenario and duration narrow the benchmark, for example
`nsi-cell-atmosphere-benchmark thermal-loaded-breaches 1` for a 10x update;
that loaded coupled case measured about **103 ms per simulated second**, so
higher playback speeds are not a real-time guarantee.

The Editor runtime benchmark additionally publishes Cell/Sector/Hull/pathway
snapshots, including derived `breach-<level>-<wall>` telemetry. On the same machine,
using the current wind/load-enabled runtime, two warmups and ten trials measured
**0.72 ms** at rest, **8.77 ms** for four breaches and **10.06 ms** for sustained
breaches per 0.1 simulated seconds. The sustained case is slightly over the
10 ms target with both optional calculations enabled; both disabled measured
**9.92 ms**. A sustained one-simulated-second update with both enabled measured
**97.4 ms** (98.0 ms maximum).
These include native gas/heat and snapshot publication, but exclude construction,
controller history/copy costs, GUI rendering and scheduling overhead; accelerated
playback still has no real-time guarantee.

```sh
cmake --build build-linux --config Release --target editor-cell-playback-benchmark -j 16
build-linux/editor/editor-cell-playback-benchmark
build-linux/editor/editor-cell-playback-benchmark sustained-breaches 1 both
build-linux/editor/editor-cell-playback-benchmark sustained-breaches 0.1 none
```

The runtime consumer follows the station-systems scenario-rig precedent and
checks isolated temperatures across ticks, active loads/couplings, and pressure
and gas-quality crossings. Editing-identity coverage is tracked by #9, and
domain records by #10.
