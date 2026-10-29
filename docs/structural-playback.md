# Opt-in native local-solid playback (#29)

`CellThermalMode::LocalStructure` selects the #27 topology and #28 numerical
solver **at construction/Reset only**. `Geometry` (shared Hull) remains the
Editor default until #35. Standalone compartment APIs and YAML are unchanged.

```cpp
nsi::systems::CellAtmosphereTransport model(installation,
    nsi::systems::CellThermalMode::LocalStructure);
// Or construct SimulationPlayback(std::nullopt, CellThermalMode::LocalStructure)
// in an Editor prototype host. No in-flight backend setter exists.
```

For consumers editing an expansion before construction, use
`expandCellAtmosphere(installation, true)`. This builds checked structural
geometry and a world-coordinate source-Cell owner map. Passing an expansion
without structural topology to LocalStructure refuses explicitly. Structural
limits/errors remain separate from gas limits; see [topology](structural-topology.md).

## Integration and observations

Each physical side maps to its existing gas Cell, including tall height claims
and offset Levels. Missing gas (Interior Void preset) omits only that film.
Local bodies start at 293.15 K with the preset's split physical capacity. Current
species cv, internal energy, wet mole fractions and pressure/temperature feed
`NativeThermalSolver` after each gas/source interval (at most 0.1 s). Returned
gas heat updates only gas internal energy and its conducted-heat ledger;
returned solid heat updates each unique `ThermalBody`. Structural capacity never
enters pressure or sound speed. Solid connections persist through evacuation.

Only one backend executes: LocalStructure constructs neither the legacy Hull
state nor its direct gas-to-gas links. `hull: none` still produces no geometry
thermal bodies/contacts. Missing Partition types remain opaque adiabatic
boundaries. Unresolved reservoirs refuse as in Geometry mode.

Exterior solid sides use their own material emissivity and temperature. Solar
input preserves the aggregate projected-area approximation: each exterior
patch receives absorptivity × area / 4 × irradiance × profile. Legacy Hull
boundary settings supply sink temperature/irradiance when present; otherwise
3 K and the existing default irradiance apply, with darkness absent an explicit
solar profile. These are provisional C1 material values, not rollout approval.

`model.structure()` and `CellPlaybackRuntime::structuralSnapshot()` provide
committed, preallocated minimum solid observations alongside existing gas
snapshots. Body order maps to `geometry().structure->assemblies[*].bodies`;
those assemblies supply stable geometry/side IDs. Bodies expose separate
capacity, stored energy and temperature. Global solid energy, gas internal and
kinetic energy, net exterior radiation/solar heat, audit residual and whole-call
work counts are distinct. Model/preset revision strings identify
`local-structure-v1-candidate` / `C1-v1-provisional`, not a save-schema change.
The facade omits legacy Hull/link snapshots for this backend. Richer spatial
inspection, rate/heat-path breakdowns and rendering remain #32/#33.

The audit is:

`initial(solids + gas internal + gas kinetic) + exterior radiation/solar
 + inventory + transported + external heat + occupant heat + latent heat
 - current(solids + gas internal + gas kinetic)`.

Inventory already subtracts leak and condensate carried sensible/kinetic energy;
latent phase heat is separately booked. Internal advection cancels globally;
remaining transported energy is exterior advection. Film and solid conduction
cancel internally, so conducted heat is deliberately **not** added again to the
global audit. The solver independently enforces its existing
`1e-7 J + 1e-12 * sum(abs(node heat))` energy tolerance and publishes separate
nonlinear/flux residuals. The complete audit also includes gas solver/source
roundoff; it is not a relative tolerance against near-zero final gas inventory.

## Transactions and work

All body, gas adaptation, exterior, solver, rollback and publication buffers are
allocated at construction. Successful transport `advance` calls allocate no
heap memory, including accelerated intervals. The Lua lifecycle stage retains
its separate allocation contract (ADR-0006); GUI histories are not claimed to
be allocation-free.

The existing thermal-budget argument is a **whole-call work** budget for the
candidate, counting all linear/nonlinear/refinement solver work. Each source
slice receives only the remaining budget; it is not reset at 10x. Per-interval
nonlinear/refinement limits retain the solver defaults. Acoustic and source
limits remain unchanged. See [solver](native-thermal-solver.md).

Any refusal restores every modified gas/body energy, source/phase/inventory,
exterior and momentum ledger, time, and last committed observations/work
counts. Flux observations are retained rather than cleared. Accepted Door gas
controls remain the attempted advance's starting state. Playback keeps the
refused duration pending even after a speed change and commits lifecycle/time
only on a successful retry. Pause/edit lockout and Reset semantics are shared
with the existing backend; Reset reconstructs fresh 20 °C solids without
mutating authoring.

## Deliberate staging

This ticket does **not** implement diffuse cavities (#30), open-leaf thermal
pose changes/breach radiation portals (#31), objects (#21), parallelisation
(#20), or default rollout (#35). Until #31, a Door control changes its gas
opening but retains the Reset-time solid leaf/film/exterior contacts and energy;
opening radiation is not yet modelled. Missing-Wall opening metadata produces
no duplicate radiating solid face, but aperture radiation waits for #30/#31.
Use this candidate for staged integration/tests, not as the completed parent
thermal model or a performance-approved production default.

## Validation

`nsi-structural-playback` tests gas/solid energy pairing and the full audit,
static/dynamic evacuation, retained unresolved inventory, chilled-structure
repressurisation through a finite neighbouring Cell, tall/offset contact owners,
Void/no-Hull/reservoir compatibility, early/nonlinear/late refusal and whole-call
budgets at 1x/10x, accepted Door retention, committed flux observations, and zero
successful-step allocations. `editor-simulation-playback` also exercises the
candidate facade's gas/solid publication, Lua rollback/retry, accelerated time,
edit lockout, Reset and retained refused-time debt. Existing topology, solver,
standalone and legacy playback fixtures remain active.

Incremental Release build and all 136 CTest tests passed with `-j 16`
(162.41 s on the development machine). This is functional validation, not the
integrated serial performance/rollout gate reserved for #35.
