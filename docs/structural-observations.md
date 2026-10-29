# Committed structural observations (#32)

Native `LocalStructure` only; default `Geometry`, standalone compartments,
physical presets and schema are unchanged. Editor rendering/histories are
covered by [#33](structural-editor.md); the integrated performance/default review
remains #35.

## Observation contract

`CellAtmosphereTransport::structure()` (Editor: `structuralSnapshot()`) is a
const view, not a per-frame copy of the surface graph. `enabled` distinguishes
the selected backend. `observedSeconds` identifies the last successful physics
commit. Revisions, unique solid energy, gas internal/kinetic energy, global audit
residual, thermal/nonlinear/linear/refinement work, accepted nonlinear/flux/energy
residuals and maximum accepted cavity closure (W) are separate diagnostics.
Counts describe allocated topology slots; closed Door slots can have zero area.
`topologyStorageBytes` is the geometry builder's conservative storage bound;
`observationIndexPayloadBytes` counts vector capacities and map value payloads,
**not** allocator/tree overhead or total solver memory.

`structuralSurface(id)` returns a stable geometry-derived assembly/side identity,
current binary-pose owner, area, Kelvin surface temperature, allocated side
capacity (J/K), and stored energy (J, Kelvin reference). Shared assemblies have
one mass split between their sides; summing unique side energies counts that
mass once. Query results also identify the local cavity and modelled gas Cell,
gas-only capacity, optional gas temperature and current pressure/mixture film
conductance (W/K). This instantaneous film conductance is not a cooldown
prediction. Unresolved gas has unavailable temperature and zero contact; missing
gas has no gas Cell. Neither condition deletes structural observations. A side
with no geometric owner is exterior. Door IDs persist when both leaf sides park
at the canonical Interior owner; closing restores the original owners without
resetting energy. Orientation describes the patch's original axis/side, not a
simulated hinge angle.

`structuralSummary(owner)` and `structuralSectorSummary(cavity)` return all,
floor, ceiling and side groups. Each group explicitly provides **area-weighted
surface temperature**, minimum/maximum side temperatures, exposed area, summed
side capacity and stored energy. There is no universal room temperature or
instant equilibration assumption. Empty groups have `surfaces == 0` and an
unavailable `areaWeightedTemperatureK()`; ignore their zero min/max placeholders.
A shared floor's positive-normal side is a floor, its negative side a ceiling.
Summaries count each selected side once, including same-Sector assemblies.
Sector summaries are **exposed-side** summaries, not full assembly mass assigned
to each neighbouring Sector. Exterior sides are included in global unique solid
energy but not in Interior exposed-side summaries. Gas EOS never uses solid
capacity; `C_gas/G` must not be labelled whole-structure cooldown.

Owners are world-coordinate source Cells (including tall claims). Geometric
cavity indices are Reset-local Sector handles, including Interior Void-preset
Sectors, **not** atmosphere array indices. `geometry().structure->cavityOwners`
maps owners to these handles. Assembly/side IDs, unlike cavity handles, do not
depend on mutable Sector indices.

## Heat-path ledger definitions

`paths` is cumulative accepted energy (J). `rates` is the last successful
**whole-call mean** power (W), including all accelerated/source slices:

| Path | Sign / aggregation |
|---|---|
| Gas into solid | Signed, positive from gas into solids; opposite gas conducted ledger |
| Solid conduction magnitude | Sum of absolute transfers over unique solid contacts |
| Interior radiation magnitude | Sum of absolute surface-to-cavity transfers; counts both surfaces of an internal exchange, not net heat loss |
| Portal escape | Signed outward through exterior portals only; reciprocal internal portal exchange cancels globally |
| Exterior radiation | Signed outward from exterior solid faces, excluding apertures |
| Absorbed solar | Positive inward, already projected and absorbed |

Thus `exteriorHeatJ = absorbedSolarJ - exteriorRadiationJ - portalEscapeJ`.
Internal magnitudes are diagnostics, **not** extra energy sources in the audit.
Path integrals use accepted implicit intervals and the same converged
coefficients as numerical heat booking, never failed nonlinear trials.
`work.paths` contains only the last committed call's integrated paths.

## Bounded graph/inspection queries

Callers supply a span and pagination offset to `structuralSurfaces(owner, ...)`,
`structuralSectorSurfaces(cavity, ...)`, `structuralContacts(surfaceId, ...)`, or
`structuralPortals(cavity, ...)`. Results report written and total counts, so the
UI can display truncation explicitly. A local graph consists of selected sides,
their gas/cavity endpoints, and immediate solid contacts or cavity portals;
there is no recursive neighbour expansion. Contact endpoint IDs can be resolved
individually, including exterior sides. Portal results identify both geometric
cavities (or exterior), actual current aperture area without hydraulic Cd,
optional committed irradiances (W/m²) and signed instantaneous A-to-B power.
The radiation model is **visibility-free, well-mixed diffuse Sector cavities**,
not detailed view factors.

Indexes build at Reset. Owner lookup is logarithmic; Cell queries visit only
local candidates, Sector summaries visit that Sector's unique candidate sides,
and contact/portal queries visit only local adjacency. Returned detail is bounded
by the supplied span. Summaries do not calculate gas film details. Opening a
Door filters preallocated endpoint candidates rather than rebuilding vectors.
No query or successful transport step allocates. Publication adds constant-size
path/rate fields plus one optional irradiance per cavity, not copies of every
surface or every graph edge. The existing body energy audit still visits all
bodies; its cost remains part of the measured update.

Only successful updates publish new paths, rates, time and irradiance. Refusal
restores gas/solid state and all path/work ledgers; queries continue to see the
last commit, not solver trial fields. Accepted Door controls persist on refusal.
Changing a Door pose invalidates irradiance/power until the next physics commit
rather than showing a stale field under the new boundary. Unanchored reflecting
cavities remain unavailable. Pause (`advance(0)` or paused Editor playback)
retains the last rate and observation timestamp; it does not invent zero-rate
samples. Reset clears ledgers/rates/time and consistently initializes bodies.
The Editor facade exposes Cell/Sector summaries and bounded surface queries;
its read-only `model()` provides contact/portal queries used by the bounded
[#33 topology browser](structural-editor.md).

## Validation and measured observation cost

Release `nsi-structural-observations` covers directional groups, area weighting,
side-capacity split, same-Sector deduplication, stable Reset identity, bounded
pagination/contacts/portals, accepted Door ownership, unavailable gas, vacuum
structure, separate heat paths/closure, early and cross-slice rollback, field
retention and allocation-free update/queries. Editor playback tests cover facade
summaries, pause, Reset, accelerated updates and refusal retention.

Reproduce the focused serial measurement (no GUI, no concurrent CTest):

```sh
cmake --build build-linux --config Release -j 16
build-linux/systems-bridge/nsi-structural-observations-test --benchmark
ctest --test-dir build-linux -C Release -j 16 --output-on-failure
```

Development machine: x86_64 AMD Ryzen AI Max+ 395, 32 logical CPUs; serial Release
code, 64×64 sealed BareSkin room, 4,096 gas Cells, 16,896 bodies, 8,448 assemblies,
42,240 solid contacts, one cavity. Two warmups, ten measured 0.1 s updates; local
query timing averages 1,000 selected-Cell queries plus summaries per update.
Final run measured construction 19.79 ms, full native update **including
commit/audit** 45.43 ms, selected-Cell detail+summary 0.000202 ms,
full-Sector summary 0.505 ms. Earlier runs measured 41.82–45.10 ms updates,
0.000190–0.000193 ms local queries and 0.422–0.427 ms Sector summaries. Query costs are separate from numerical
update; no whole-topology detail serialization occurs. This is a focused cost
measurement, **not** the #35 scenario/1x/10x/memory/GUI matrix or an attribution
of the full update cost to observations. The integrated update misses the parent
≤10 ms target; no fidelity/default/budget relaxation is approved by this ticket.

Final incremental Release build and all 137 CTest tests passed (`-j 16`, 163.60 s),
including the portal queries and expanded ownership/rollback fixtures.
