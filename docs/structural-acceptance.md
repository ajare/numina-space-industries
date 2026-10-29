# Native structural thermal acceptance (#34)

Traceability for every acceptance item in [#22](https://github.com/ajare/numina-space-industries/issues/22), section 10, in its original order.
This consolidates component coverage, not a substitute for it. Tests select
LocalStructure explicitly; Geometry remains the default. No standalone runtime,
physical default, work budget or fidelity change is authorised here.

Rollback asserts exact retained conserved inventories, body energy, heat ledgers,
committed fields and time. Derived gas temperature/film queries are rebuilt from
the retained inventory; these allow eight machine epsilons of scaled roundoff
because cached heat-update and full primitive reconstruction use different
floating-point evaluation paths (observed temperature difference 5.68e-14 K).
This is not a physical accuracy or energy-residual relaxation.

| # | Acceptance requirement | Automated test / explicit gate |
|---|---|---|
| 1 | Isolated gas + two solids, weighted equilibrium, same-temperature addition | `station-native-thermal-consumer`: isolated 200-step gas/two-body audit and equilibrium; disconnected/same-temperature addition. `station-thermal-body-consumer`: energy initialization and gas EOS separation. |
| 2 | Unique shared Wall/floor area/mass; tall/offset owners; stable IDs/reset | `nsi-structural-topology`: two-sided capacity/area invariants, stepped/tall/offset floors/Walls, index reshuffle and fresh Reset. `nsi-structural-playback`: tall owner gas booking; `nsi-structural-observations`: unique surface/global capacity observations. |
| 3 | Persistent finite-path local gradients | `station-native-thermal-consumer`: sparse cycle with persistent gradient and unchanged disconnected patch; `nsi-structural-observations`: local cold patch, surface range and interior path. |
| 4 | Passive calibration and approved flux/transfer targets | `nsi-thermal-calibration`, `nsi-core-thermal-presets`, `station-thermal-body-consumer`: positive/passive coefficients and six reference anchors within 5%. Candidate engineering values documented in [thermal-calibration.md](thermal-calibration.md). **Approval**, not numerical test success, remains #35. |
| 5 | Continuous pressure sweep, vacuum limit, persistent solid conduction | `station-thermal-body-consumer` logarithmic film sweep; `nsi-thermal-calibration` mixture/temperature/pressure sweep; `station-native-thermal-consumer` zero-pressure/unresolved detachment with continuing conduction/radiation. |
| 6 | Evacuated structure retains energy and evolves without gas | `nsi-structural-playback`: exact static/dynamic evacuation, trace inventory retention, closed gas-free cavity redistribution. `nsi-structural-s0`: drained owner S0 retains capacity and continues cooling. |
| 7 | Warm versus chilled repressurisation, both audits close | `nsi-structural-playback`: finite neighbouring warm Cell, initially empty receiving Cells, open Door; admitted gas colder beside 250 K solids than warm solids, both global audits. No infinite reservoir or invented gas capacity. |
| 8 | Closed isothermal cavity, reciprocal conservation, aperture bound | `station-native-thermal-consumer`: analytic enclosures, reciprocal portals, 12-cavity chain, unanchored/reflecting fields, time refinement and sparse nonlinear closure; `nsi-structural-playback`: sealed cavity and black-aperture bounds with physical-height breach. See [diffuse-radiation.md](diffuse-radiation.md). |
| 9 | Energy-preserving Door, 1.8 m² without Cd, authoring unchanged, same-Sector portal | `nsi-structural-playback`: 100 open/close cycles without allocation/time/energy change; exact restored closed mapping, exterior orientations, independent gas/cavity parking, Interior Void/no-Partition endpoints, same-Sector no sink, tall Door and 12 m² breach. `nsi-structural-observations`: accepted-control ownership and invalidation. |
| 10 | Portable original S0, pressure/gas-solid/evacuated evolution/closure | `nsi-structural-s0`, [fixture provenance and stimuli](../systems-bridge/tests/fixtures/README.md). Original available and captured byte-for-byte; no home-path access at test time. Existing saved exterior Door supplies blowdown; separately labelled controlled drain provides exact evacuation, not an invented natural vacuum time or monotonic gas curve. |
| 11 | Hull:none, Interior Void vs geometric Void, missing reservoirs, schema and standalone | `nsi-structural-topology`, `nsi-structural-playback`: no-Hull opt-out, gas-free Interior retains cavity/structure, geometric Void has neither floor nor solids, missing reservoir refuses; no-Partition opaque/adiabatic closed boundary and open Door portal. `nsi-core-serialization`: old schemas, enum/preset identifiers and round trips. `serialization-consumer`, `station-library-runtime`, all `station-library-scenario-*` and `nsi-systems-bridge-runtime-consumer`: unchanged standalone runtime/configuration. |
| 12 | Early/late/nonlinear whole-call refusal, retained time at 1x/10x; zero successful numerical allocation | `nsi-structural-playback`: 0.1/1 s early/nonlinear/late failures, cross-slice budget failure, gas/body/source/phase/momentum/flux and heat-path/rate/energy/surface/portal-field publication retention; accepted Door control survives and retry succeeds. `nsi-structural-observations`: field retention. `editor-simulation-playback`: lifecycle + physics atomicity, refused-duration debt across speed change, no history/time commit. Allocation counters surround successful transport/retry, Door controls, solver and bounded queries in the above tests and `station-native-thermal-consumer`. Construction, fixture I/O, test snapshots, GUI and Lua lifecycle allocation are deliberately outside the numerical guarantee. |
| 13 | Separate air/surface UI, paths, unresolved gas unavailable | `editor-structural-view`, `editor-installation-render-mode`, `editor-sector-thermal-panel`, `editor-simulation-playback`: explicit surface label/aggregation, grey unresolved gas, separate energies/paths, bounded topology, simulation-time histories/gaps, pause/Reset/refusal. Manual #35 gate: interactive rendering/readability, full GUI/GPU cost and optional wind/force/save behaviour review; headless tests do not establish visual quality. |
| 14 | Release CTest and serial performance/memory before default rollout | Full incremental Release build and `ctest --test-dir build-linux --output-on-failure -j 16` required here. **#35 remains open:** record development hardware and integrated 4096-Cell rest/cooling/gradients/four-breach/sustained-vacuum/fully-evacuated/repressurisation/many-Sector/tall-offset matrix at 0.1 and 1 s; separate construction/numerics/publication/GUI, work and memory/node/link counts, revision identifiers. Prior measurements in [structural-cost-review.md](structural-cost-review.md) and [structural-observations.md](structural-observations.md) are not rollout approval. Review missed ≤10 ms target explicitly; no automatic simplification or hidden limit change. |

## Validation record

Incremental Release build and all **139/139** CTest tests passed with `-j 16`
(162.96 s, 2026-10-29 development workspace). Focused S0/playback/observation
checks also passed. Successful transport/accelerated retry/solver/query allocation
counters remain zero. This functional run does not satisfy the outstanding #35
integrated performance, interactive UI or physical-default approval gates.

## Running

```sh
cmake --build build-linux --config Release -j 16
ctest --test-dir build-linux --output-on-failure -j 16
```

The S0 test takes its fixture as a CTest argument resolved from the source tree,
so it is independent of the invoking directory and developer home. The fixture
contains geometry/presets only. All energy comparisons use physical accounts,
not prescribed cooling curves. LocalStructure replaces rather than supplements
the legacy Hull path. Standalone tests continue to run in the full suite.
