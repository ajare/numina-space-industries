# Local structural thermal state for native playback

**Policy amendment — sole native backend:** The owner explicitly requested
LocalStructure as the only native thermal backend and removal of Geometry and
the Editor backend selector. This supersedes the prototype/default-retention
policy below: native playback and reusable transport now default to
LocalStructure, with no legacy shared-Hull/direct-gas heat path. The Editor has
no backend-selection API. Explicit Adiabatic transport remains solely for
isolated gas tests/benchmarks; standalone compartment behavior and YAML are
unchanged. This is a deliberate rollout decision despite the unmet measured
performance target, not a claim that the gate passed. Work budgets, energy
bounds, fidelity and refusal/rollback contracts are unchanged.


**Status: Accepted architecture contract; implementation and physical defaults pending review gates.** Recorded for [#23](https://github.com/ajare/numina-space-industries/issues/23), under the authoritative [#22 specification](https://github.com/ajare/numina-space-industries/issues/22). This amends [ADR-0002](0002-geometry-authored-thermal-coupling.md) and the thermal representation in [ADR-0004](0004-cell-atmosphere-and-horizontal-momentum.md) **for native Editor playback only**. It does not claim the upgraded backend is implemented or approve its default rollout.

The current single Hull temperature and gas-mediated partition links cannot retain local structural thermal history after evacuation. Replace those native geometry heat paths with finite-capacity local solids, pressure-dependent gas contact and diffuse cavity radiation, rather than adding hidden capacity to gas or running two geometry heat models together. The standalone compartment runtime retains its existing behaviour and ADR-0002 representation.

## Ownership and dependency direction

- **Core** owns authored Installation geometry, Hull/Partition choices and versioned intensive material/preset descriptions. It remains independent of simulation and Lua. No temperatures, runtime inventories or numerical solver state belong in core.
- **systems-bridge** expands geometry/presets into deduplicated assemblies, side ownership, physical contacts, gas contacts, cavities and portals at Reset. Stable assembly/surface identifiers derive from physical geometry and side ownership, not mutable Sector indices. The bridge hosts playback orchestration and snapshot adaptation, not a second numerical implementation.
- **station-systems** owns native numerical body/contact state, conserved inventories, sparse conduction/contact/radiation integration, energy ledgers, deterministic work budgets and rollback. Its reusable finite-capacity body/contact interface may later serve objects without importing Installation geometry.
- **Editor** owns authoring, Reset and observations. Separate gas and surface temperatures, capacities, stored energies and heat-path ledgers must be labelled explicitly; structural temperature is not a substitute for unresolved gas temperature. Histories use committed simulation time and unavailable observations are gaps. Topology displays must be bounded rather than recursively drawing every surface.

This is a target ownership contract, not a description of completed migration: the existing bridge `CellAtmosphereRuntime` currently holds the shared-Hull thermal state/orchestration. Later implementation tickets move the new numerical state behind station-systems APIs without reversing dependencies. Public names remain `station::`, `StationSystems::StationSystems` and `Promethium::Serialization`.

## Local structure is independent of gas

Create local Hull panels, shared Wall/floor/ceiling assemblies and Door leaves/frames at existing Cell-face/Level boundaries. Deduplicate each physical shared assembly and its mass across both sides, Levels, offsets and tall claims. The initial candidate is two finite-capacity side nodes per panel, sharing the panel's total areal capacity rather than receiving that capacity twice. This discretisation remains subject to the calibration/cost prototype; it is not permission for an arbitrary fine mesh.

Through-assembly resistance and finite local in-plane/junction contacts connect actual structure. Mere proximity creates neither contact nor a hidden station-wide isothermal bus. Gas couples to its local surface; partition conduction does not require resolved gas on either side. Evacuation removes no structural capacity, energy or solid connection. Unresolved gas detaches while retaining its conserved inventory. Structural capacity never enters gas pressure, sound speed, composition or EOS heat capacity.

New structural bodies start at **20 °C (293.15 K)** unless an explicit existing boundary condition supplies a different initial value. Construction/Reset initializes energy consistently from capacity and temperature; it is not an applied heat source. Door actuation preserves leaf capacity, temperature and energy rather than initializing a replacement body. Opening relocates leaf surfaces using a documented deterministic binary-pose approximation, retains the frame and removes aperture area from the fixed solid budget exactly once; closing restores ownership without resetting state.

Gas contact must be continuous, finite, nonnegative and tend to zero at zero pressure. The first candidate is a continuum/free-molecular resistance bridge with a documented mixture/temperature/accommodation-dependent coefficient, not an undocumented transition-pressure clamp. Only the gas film scales with pressure: solid resistance/conduction and radiation remain. Every gas heat transfer has an opposite structural booking. Reheating of remaining gas is physically possible; monotonic gas cooling is not a contract.

## Geometry, Void and radiation boundaries

An **Interior with the Void atmosphere preset** still has a floor and may have structural bodies and a radiation cavity. Omit its unmodelled gas contact; the missing compartment declares neither vacuum nor an exterior gas/heat sink. Determine its radiation boundaries from geometry. **Geometric Void** has no floor or Interior volume of its own; bordering surfaces retain the existing exterior-boundary convention. These two meanings of Void must not be merged.

Use one diffuse, well-mixed algebraic radiative field per geometric Interior Sector as the initial cavity approximation, including Interior Void-preset Sectors. It is not stored energy, a gas temperature or a zero-capacity thermal node to be detached. Radiation operates while pressurised too. With irradiance `H_s` and surface temperature `T_i` in Kelvin:

`sum_i epsilon_i A_i (sigma T_i^4 - H_s) + sum_portals A_portal (H_neighbour - H_s) = 0`.

Surface exchange and reciprocal portal exchange conserve energy internally. Exterior fields use `sigma T_space^4`; exterior solid faces radiate locally using their own temperatures/emissivities and existing solar inputs. No conductive link to vacuum is created. A same-Sector portal is not a new sink. An isothermal room's aperture loss cannot exceed the black-aperture limit `sigma A_portal (T_room^4 - T_space^4)`. A closed isothermal cavity has zero net exchange; a component with neither emitting/absorbing surfaces nor boundary exchange transfers zero heat and reports irradiance unavailable rather than inventing a temperature or failing singularly.

The diffuse approximation ignores intra-Sector visibility/occlusion and must be named as such in observations. Use sparse/algebraic or matrix-free coupling, never a dense all-surface view-factor matrix. Open Doors use actual clear area **1.8 m²**, not hydraulic Cd-adjusted area; closed leaves block the portal. Missing-Wall breaches produce radiative apertures, not duplicate solid faces. Thermal opening areas respect physical height claims while gas openings keep ADR-0004's fixed horizontal area.

## Compatibility and preserved native contracts

- `hull: none` remains the explicit geometry-thermal opt-out: no new geometry thermal mass, contacts or cavity heat model in that mode. Missing Hull properties still mean no Hull, not the new-Installation default.
- With no Partition type, shared partitions remain **opaque adiabatic boundaries**. Invent neither through-Wall conduction, structural capacity nor a radiation portal there. In the cavity approximation these unmodelled faces reflect without absorbing/emitting; geometry still blocks gas and direct radiation.
- Existing Hull/Partition enum values and YAML identifiers remain loadable. Versioned preset descriptions may extend physical properties without requiring raw per-surface authoring. Any wire-format change requires explicit migration tests; a physical-model revision is not itself a save-schema revision.
- ADR-0004's Cell storage (`12 m³ × Sector height`), horizontal gas openings, no vertical gas flow, conserved gas state and missing-reservoir refusal remain. Authored geometry is immutable during playback. No standalone compartment/plant gas or geometry heat ledger runs concurrently with native physics. [ADR-0006](0006-native-playback-lifecycle-stage.md)'s lifecycle/mirror stage remains distinct from numerical processing.
- Native numerical loops remain C++ and successful numerical steps allocate no heap memory after construction. Preallocate sparse topology, radiation, iteration and rollback buffers; validate counts and checked allocation sizes before run construction. Any structural limit must be reported separately, not borrowed from the gas-compartment limit or used to omit unpressurised structure silently.
- Thermal/source intervals remain bounded to at most 0.1 simulated seconds with passive implicit integration, bounded nonlinear iteration/refinement and deterministic whole-call work budgets. No temperature clamps, dropped heat/time or hardware wall-clock refusal. Whole-update failure restores gas, solids, ledgers and time, retains the refused duration and last committed observations, and preserves previously accepted Door controls. Retain the thermal energy/flux residual scale `1e-7 J + 1e-12 × sum(abs(applied node heat))`, with separate nonlinear residual reporting.
- Count each solid's energy once. Internal film, conduction and cavity transfers cancel; the audit includes gas internal/kinetic energy, unique solid energy, existing source/phase/inventory/condensate accounts, exterior advection and net radiation. No automatic crew/cargo mass or object placement is introduced; #21 remains separate. Parallelisation (#20) is also out of scope.

## Revision diagnostics and backend policy

Diagnostics and benchmark records must identify the selected native thermal backend, **thermal-model revision and material/preset revision**, separately from save-schema version. Report node/link/memory counts, work/refinement counts, residuals and explicit refusals so changed physical defaults can be distinguished from data migrations and numerical failures.

Prototype comparisons may select one backend **at Reset only**. Reset constructs a fresh consistently initialized state; no mid-run swap may create/delete structural energy. The upgraded backend replaces, never supplements, the old shared-Hull/direct-gas geometry path. The sole-backend policy amendment above supersedes retention of the old default. Slow hardware must never trigger automatic fidelity reduction, state merging, hidden work-limit increases or backend substitution.

## Calibration and measured-performance gates

Architectural acceptance here authorizes the scoped calibration and cost work, **not numeric defaults or default rollout**. The parent specification remains authoritative; amendments to its targets require explicit review.

1. **Calibration (#24):** derive positive/passive solid/film/radiative resistance splits, capacities and gas-contact coefficients. Do not reuse effective U-values as bare solid conductance and double-count films/radiation. Verify ADR-0002's Hull flux anchors (~224, ~62, ~3.8 W/m²) and partition-transfer anchors at documented reference conditions, with the parent's proposed 5% steady-state tolerance. Preserve the initial total exterior-panel capacity anchor and document new partition/frame capacities and transient effects. A missed target requires a spec/ADR amendment, not negative resistance or a hidden clamp.
2. **Serial cost prototype (#25):** compare local panel/cavity discretisations and record node/memory estimates against the current default wind/load-enabled runtime, including snapshot publication. The parent's ~10.06 ms per 0.1 simulated seconds for 4,096 Cells is a historical baseline, not spare headroom or a fresh measurement. Calibration and measured cost gate the versioned material/body API work (#26).
3. **Rollout (#35):** after component and end-to-end regression coverage, record Release build/CTest (`-j 16`) and the serial performance/memory matrix on documented development hardware. The target remains **≤10 ms per 0.1 simulated seconds for 4,096 Cells**; it is a measurement/review gate, not a guarantee. Measure rest, normal cooling, gradients, four breaches, sustained/fully evacuated rooms, repressurisation, many small Sectors and tall/offset Levels, at both 1x and 10x-duration updates. Separate construction, numerical work, snapshot publication and GUI costs. Include work counts and memory/node/link sizes. Missed calibration, accuracy or performance targets require an explicit accuracy/budget decision before default rollout, never an automatic reduction in fidelity.

This documentation-only ticket changes no backend, defaults or authored files. Numerical, geometry, observation and regression implementation remains in #24–#35; the acceptance suite in #22 is not satisfied merely by recording this contract.
