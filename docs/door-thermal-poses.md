# Native Door binary poses and breaches (#31)

The Reset-selected `LocalStructure` candidate uses model revision
`local-structure-v3-door-candidate`. Presets remain `C1-v1-provisional`;
standalone compartments, YAML and the default Geometry backend are unchanged.
This implements #31 under ADR-0007/#22, not rollout approval (#35).

## Binary pose approximation

A closed Door is the original two-sided leaf plus fixed frame/above-aperture
structure. Its radiation portal has zero area. Opening activates exactly
**1.8 m²** of reciprocal cavity or exterior aperture, without hydraulic Cd.
Both leaf surfaces move to the canonical negative-side Interior endpoint's
cavity and gas Cell; if that endpoint is exterior, use the positive-side
Interior endpoint instead. Ownership depends on physical face orientation,
not mutable Sector indices or presence of gas. If the chosen Interior has the
Void atmosphere preset, omit both parked gas contacts; never substitute the
other endpoint or exterior space for missing gas. Same-Sector portals are inert.

This is a deliberate binary-pose approximation, not hinge kinematics, detailed
visibility or a parked-leaf solar model. The open leaf has no direct exterior
radiation/solar exposure; both sides exchange with the parking cavity. Its
finite through/hinge conduction is retained. Closing restores original gas,
cavity, exterior radiation and projected-solar ownership, without changing
body IDs, temperature, energy or capacity. Repeated controls are idempotent.

Fixed frame area is the physical face area minus 1.8 m² once. Tall portions
above the Door remain solid. Opening relocates rather than deletes leaf area:
the leaf no longer occupies the aperture but remains a material body inside.
Even with no Partition material, an open Door enables its portal; a closed
unmodelled partition stays opaque and has no invented leaf mass.

## Construction, actuation and refusal

Reset preallocates surface, gas and exterior slots, zero-area Door portals,
and sparse field coefficient/connectivity workspaces. `setDoorOpen()` changes
only these slots and the existing gas opening. Field diagonals, boundary terms
and anchored components are rebuilt in-place after the batch. No successful
actuation or numerical advance allocates memory. Controls advance no time and
mutate no authored Installation data or committed thermal observations.

Accepted controls are starting boundary conditions, not part of an attempted
advance's rollback. Early, late and cross-slice refusal restores bodies, gas,
ledgers and observations while retaining the Door pose for retry. There is no
mid-step actuation or topology reconstruction.

## Breaches and coverage

Missing-Wall exterior apertures use structural world-height patches, each
2 m × 3 m. A height-two breach therefore exposes 12 m² thermally while its gas
opening deliberately remains 6 m². Shared/offset claims determine endpoint
ownership; an aperture has no duplicate solid panel. This reuses the #27/#30
physical opening expansion rather than estimating area from gas faces.

`nsi-structural-playback` covers two-Interior, exterior in both orientations,
same-Sector, unmodelled Interior and no-Partition endpoints; independent film
and cavity tests verify deterministic parking of both leaf sides. It checks
100 open/close cycles without allocation or body changes, restored closed
thermal evolution, no actuation time/authoring changes, clear-area aperture
bounds, tall/offset Doors, height-two breach loss/area, refused advance/retry,
and the global energy audit. Existing 1x/10x rollback and allocation tests
now run with active thermal Door controls.

Validation: full incremental Release build and all 136 CTest tests passed with
`ctest --test-dir build-linux -C Release -j 16 --output-on-failure` (161.42 s).
This is correctness/component evidence, not the serial #35 performance gate.
