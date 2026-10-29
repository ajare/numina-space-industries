# Integrated serial thermal rollout review (#35)

**Gate not passed. Production rollout is not approved.** The reusable playback
APIs still default to Geometry. After this measurement, the owner explicitly
requested LocalStructure enabled for hands-on testing: the Editor now selects it
at startup, with a visible performance/refusal warning and Geometry available in
the backend dropdown. This testing selection does not approve the gate or change
solver limits; changing backend still performs a full Reset. This
report requests an explicit owner decision under [#22](https://github.com/ajare/numina-space-industries/issues/22)
and [ADR-0007](adr/0007-native-structural-thermal-contract.md). It is not permission
to change the accuracy target, work limits, physical defaults or serial policy.
#20 (parallelisation) and #21 (object inventory/placement) remain independent/open.

## Reproduce

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --config Release -j 16
# Run alone, not concurrently with builds/CTest.
/usr/bin/time -v taskset -c 4 \
  build-linux/editor/editor-integrated-thermal-benchmark 5 \
  > integrated-thermal.csv
ctest --test-dir build-linux -C Release --output-on-failure -j 16
```

`editor/tests/IntegratedThermalBenchmark.cpp` is Linux-only evidence tooling
(`malloc_usable_size` accounting), not a portable public API or timing assertion.
Its `1 8` invocation is a small functional CTest smoke matrix. The new
`editor-reset-expansion` test separately compares ordinary and explicit Reset
construction and queried/unqueried evolution at 0.1/1 seconds. The explicit
expansion constructor is Editor-internal, requires matching authored geometry,
and accepts initial inventories only at construction. No mid-run injection or
backend swap is added.

### Hardware, build and revisions

Development host: AMD RYZEN AI MAX+ 395 (16 cores/32 threads, 64 MiB L3),
122 GiB RAM, Linux 7.0.0-38-generic x86-64, GCC 15.2.0, CMake Release
`-O3 -DNDEBUG`, no special architecture flags/LTO. One serial process pinned to
logical CPU 4; boost enabled and frequency/governor unchanged. No parallel
numerical work. Source is `1f37903276c3cfc8717420fc9b4024001ccbeb87` plus the
#35 commit containing this report/tooling. Thermal/numerical implementation is
unchanged from that source revision. Output records:

- Geometry: `legacy-native`, legacy materials.
- LocalStructure: `local-structure-v3-door-candidate`, `C1-v1-provisional`.
- Save schema 4, distinct from thermal/preset revision.
- Both Wind diagnostics and reference-object forces enabled, unchanged acoustic
  and thermal whole-call budgets of 4096.

Each row averages five fresh constructions/attempts after two discarded warmups.
Preparation is untimed with respect to the measured update, but its own mean
cost is reported. Max update cost is the largest measured attempt, not a
statistical bound. Refusals are deterministic across trials and are recorded,
not removed from the sample or retried with higher limits. This is a development
sample, not a hardware-independent guarantee.

## Matrix and timing boundaries

Every full-size fixture has 4,096 active Interior floor Cells. No developer-home
input is needed. The original #25 fixture generator is reused, with controlled
initial-state changes through the real production expansion/runtime:

| Scenario | Initial state / untimed preparation |
|---|---|
| rest | Sealed 64×64, 293.15 K, MultiLayerBlanket/InsulatedPartition. First update already includes ordinary cooling; not an adiabatic zero-cost rest case. |
| cooling | Same, after one simulated second of no-breach cooling. |
| loaded | Sealed room, 4,096 occupants and the #25 O2/CO2/moisture/100 W-per-person loads. |
| gradient | Left-half gas 313.15 K, remaining gas and structure 293.15 K; conserved energy initialized consistently, then full transport and thermal evolution. |
| breaches | Four exterior corner openings, 6 m² each. |
| sustained-breaches | Four openings after one simulated second of blowdown. |
| sustained-vacuum | Four openings; initial gas conserved quantities scaled by 1e-8 (about 0.001 Pa), then one simulated second. Low-pressure **resolved** gas, not exactly empty gas. |
| evacuated | Explicit 1e9 kg/s aggregate controlled inventory drain, then one simulated second; every primitive checked unresolved. Persistent solid energy/capacity retained. Not a natural breach-to-vacuum time claim. |
| repressurisation | Two 32×64 rooms separated by a supported closed midline Wall/Door; receiving gas initially empty, finite warm neighbour, Door opened before timing. LocalStructure receiving-side surfaces initialized at 250 K. Geometry has no corresponding local solid state, so cost comparison is not physical equivalence. |
| many-sectors | 128 closed 4×8 rooms, the unchanged gas-compartment limit. |
| tall-offset | Two 32×64 floors: lower height two, empty intermediate Level, upper floor offset (16,8); 4,096 active Cells, 6,144 authored positions. |

Both durations are single host calls: **0.1 seconds and 1 second**, not ten GUI
frames. Preparation advances real physical time, never patches a live runtime.
All candidate successful rows check continuing solid evolution, including
no-breach/unselected/fully evacuated cases, and accepted elapsed time. Refused
rows check no committed time or solid-energy change.

- `build_ms`: expansion, scratch/rollback/index allocations, lifecycle runtime
  construction and initial snapshot. Fixture YAML creation/loading is outside.
- `solver_ms`: actual facade timing around `CellAtmosphereTransport::advance`,
  including internal Sector/structural committed-state aggregation and audit.
- `lifecycle_ms`: lifecycle prepare, measured in that same update.
- `publication_remainder_ms`: total successful facade update minus those two
  stages. Includes snapshot publication, Wind/force diagnostics, lifecycle commit
  and bookkeeping. **Not** a pure snapshot copy microbenchmark. Refused rows have
  no successful timing sample: these stage columns are `NA`, never stale samples
  from preparation.
- `update_ms`/`max_update_ms`: whole accepted update or whole refused attempt.
  `per_0.1s_ms = update_ms × 0.1 / seconds` only for accepted calls; refused calls
  have `NA`, not a claimed throughput. Failed work counters are unavailable through
  committed observations, so zero/prior counts on a refused row are **not** the
  amount of attempted work. The diagnostic is authoritative for refusal.
- `gui_cpu_ms`: real headless ImGui/ImPlot selected-Sector inspector, bounded
  structural topology and histories, timing panel, plus Level-0 grid **colour
  generation** (not grid drawing). Two warmup frames then five timed frames,
  1400×2400 display, 1200×2200 panel. SimulationPlayback is constructed separately
  from authored presets, with one ordinary 0.1/1 s update and short populated
  histories. Controlled gradient/vacuum/chilled state and breach preparation are
  **not** injected into this separate UI sample. These are representative geometry
  costs, not exact trajectory UI timings or full-history worst cases.
- `preset_editor_update_ms`: one separate, non-averaged SimulationPlayback update
  on that authored-preset state (1x/10x), including facade publication **and**
  all-Sector history aggregation. Not additive to the main row, not a paired
  solver comparison. Grid colours/panel frames are timed afterward.
- SDL/OpenGL GPU submission, full grid draw, presentation/vsync, interactive
  readability and saturated-history performance remain **unmeasured**. Headless
  frames are not interactive rollout approval.

## Memory and work accounting

`cpp_live_bytes` is the net live usable allocation size after construction;
`cpp_build_peak_bytes` is the maximum during construction, both relative to the
pre-construction baseline. The benchmark intercepts scalar/array/aligned C++
new/delete throughout the process. It includes actual native gas/solid solver,
rollback, duplicate topology, observation indexes, facade snapshots and C++
lifecycle objects, rather than just `sizeof(runtime)` or P1 scratch estimates.
It excludes Lua/SDL/direct-malloc allocations, allocator metadata/retained arenas,
stack and GUI allocations. Peak process RSS is reported separately; it is not
an isolated per-model memory requirement. Counter bookkeeping allocates nothing;
the existing allocation-counter tests enforce the successful native numerical
no-allocation contract separately (not the Lua/GUI/facade allocation policy).

`topology_bound_bytes` is the bridge's conservative topology/scratch allocation
bound, not measured resident memory. `observation_payload_bytes` is vector/map
payload, not map allocator/tree overhead. Structural count columns are **local
solid** counts; Geometry zeroes do not deny its one shared Hull body. Gas slots
are reported independently. Contact counts include preallocated Door slots,
not only currently active paths. Work/iteration counts are committed whole-call
thermal counts; acoustic steps are separate. Raw residuals distinguish nonlinear,
linear flux, thermal energy and cumulative global accounts.

`cumulative_ledger_magnitude_J` is contextual magnitude from cumulative paths and
Sector net source/inventory/transport ledgers. It is **not** the solver's
`sum(abs(applied node heat))`: internal Cell transfers cancel in a Sector ledger.
Do not use this proxy or almost-zero final evacuated gas energy to invent a new
acceptance tolerance. Every successful thermal solve still independently enforces
`1e-7 J + 1e-12 × sum(abs(applied node heat))`. Whole-global residuals are recorded
separately, including large-inventory cancellation during the controlled drain;
no benchmark assertion relaxes or replaces production thermal closure.

## Coefficient reuse and idle-path review

No caching/shortcut optimization, fidelity fallback or budget increase was added.
Inspection of the measured implementation establishes the current boundaries:

- Solid topology/material coefficients and workspaces are Reset-built.
  `NativeThermalSolver::freeze` recomputes pressure/mixture/temperature films,
  radiation secants and the Jacobi diagonal at each nonlinear iterate. It does
  **not** reuse a stale nonlinear preconditioner or silently freeze films.
- `DiffuseRadiation::updateCoefficients` retains static cavity connectivity and
  area/emissivity coefficients; Door controls explicitly rebuild supported
  coefficients/ownership in preallocated storage. Nonlinear radiation offsets
  and fields are recomputed, not reused as an idle state.
- Acoustic rest/local activity is a gas-transport optimization, not a thermal
  skip. Every source slice still calls `structuralStep`; no selection/breach
  predicate gates it. The new Reset regression proves identical conserved gas,
  body energy, paths, work and residuals with/without repeated selected-Cell/Sector
  queries at both durations. It also proves a nonuniform evacuated structure
  evolves while acoustic work takes its resting zero-substep shortcut.
- Existing `station-cell-fluid-consumer` tests cover uniform rest, contacts,
  source/reference rebasing and near-vacuum activity; native-thermal tests cover
  stiff sparse/connected-cavity cases, refinement, pressure/mixture changes and
  allocation-free success. Structural Door tests cover exact close/cycle
  equivalence, changed aperture/contact ownership, conservation and refusal.
  See [acceptance traceability](structural-acceptance.md) for the full suite.

These checks validate the implemented reuse boundaries, not hypothetical future
optimizations. Do not extrapolate initial acoustic rest to later cooling costs.

## Measured results and validation

[Raw 44-row CSV](benchmarks/integrated-thermal-gate.csv) and
[process resource report](benchmarks/integrated-thermal-gate-time.txt) are checked
in. The run completed in **70.15 seconds**, peak RSS **49,256 KiB**. All 44 rows
were attempted; **43 accepted**, one deterministically refused. No work limit was
raised. All 21 accepted LocalStructure rows exceed the ≤10 ms / 0.1 s target.

Times below are ms. Geometry and candidate final columns are normalized to
0.1 simulated seconds; `Update/attempt` is the full duration. `Pub` is the
publication remainder, and GUI CPU has the representative-state exclusions above.

| Scenario | s | Geometry /0.1 s | Candidate build | Solver | Pub | GUI CPU | Update/attempt | Candidate /0.1 s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| rest | 0.1 | 0.779 | 25.167 | 23.248 | 0.150 | 1.427 | 23.542 | 23.542 |
| rest | 1 | 7.119 | 25.518 | 276.885 | 0.217 | 1.499 | 277.224 | 27.722 |
| cooling | 0.1 | 8.586 | 25.315 | 30.701 | 0.199 | 1.437 | 31.182 | 31.182 |
| cooling | 1 | 8.105 | 25.228 | 313.638 | 0.207 | 1.441 | 314.129 | 31.413 |
| loaded | 0.1 | 1.568 | 25.058 | 24.171 | 0.137 | 1.505 | 24.424 | 24.424 |
| loaded | 1 | 8.008 | 25.353 | 312.754 | 0.211 | 1.477 | 313.080 | 31.308 |
| gradient | 0.1 | 7.607 | 25.155 | 24.841 | 0.140 | 1.359 | 25.089 | 25.089 |
| gradient | 1 | 7.902 | 25.621 | 256.183 | 0.198 | 1.628 | 256.511 | 25.651 |
| breaches | 0.1 | 7.381 | 25.362 | 32.897 | 0.180 | 1.382 | 33.197 | 33.197 |
| breaches | 1 | 9.817 | 26.323 | 335.805 | 0.212 | 1.443 | 336.143 | 33.614 |
| sustained-breaches | 0.1 | 10.468 | 26.034 | 35.754 | 0.189 | 1.501 | 36.242 | 36.242 |
| sustained-breaches | 1 | 9.872 | 25.772 | 358.174 | 0.205 | 1.541 | 358.677 | 35.868 |
| sustained-vacuum | 0.1 | 10.304 | 25.325 | 30.264 | 0.206 | 1.481 | 30.769 | 30.769 |
| sustained-vacuum | 1 | 9.809 | 25.899 | 307.069 | 0.215 | 1.596 | 307.579 | 30.758 |
| evacuated | 0.1 | 0.461 | 25.315 | 19.517 | 0.081 | 1.499 | 19.887 | 19.887 |
| evacuated | 1 | 0.301 | 25.434 | 194.921 | 0.090 | 1.403 | 195.300 | 19.530 |
| repressurisation | 0.1 | 30.975 | 25.428 | 172.603 | 0.204 | 1.188 | 172.950 | 172.950 |
| repressurisation | 1 | 18.019 | 25.491 | NA | NA | 1.068 | **985.189 refused** | NA |
| many-sectors | 0.1 | 4.708 | 34.091 | 27.552 | 0.156 | 0.757 | 31.850 | 31.850 |
| many-sectors | 1 | 5.535 | 33.936 | 308.869 | 0.178 | 0.681 | 312.965 | 31.296 |
| tall-offset | 0.1 | 0.985 | 26.848 | 22.184 | 0.136 | 0.735 | 22.456 | 22.456 |
| tall-offset | 1 | 7.216 | 26.884 | 266.495 | 0.216 | 0.696 | 266.855 | 26.686 |

The fresh default sustained-breach baseline is **10.468 ms**, versus the #25
capture's **10.279 ms** and the parent's historical ~10.06 ms. None is spare
headroom. The integrated candidate's 36.242 ms is a replacement-backend
measurement, not baseline + isolated P1 cost. Fully evacuated structure still
costs ~19.5–19.9 ms / 0.1 s: deleting solids/thermal time is not an optimization.
Cold repressurisation needs substantially more nonlinear/linear work and is
**not** a throughput guarantee at either duration.

The 1-second repressurisation diagnostic is
`thermal algebraic field solve/work budget exhausted`. All seven attempts
(two warmups/five measured) refuse with retained starting time and solid energy.
Mean attempted cost **985.189 ms** is not an accepted update cost; no final
snapshot/history is published. Existing detailed rollback tests cover the
other conserved quantities, fields and accepted controls. This is a separate
rollout blocker from the measured performance misses.

### Integrated storage and work envelope

Representative LocalStructure C++ residency (excludes direct malloc as above):

| Geometry | Assemblies / solid bodies | Solid / film / exterior contacts | Cavities / portals | C++ live MiB |
|---|---:|---:|---:|---:|
| flat | 8,448 / 16,896 | 42,240 / 8,448 / 8,448 | 1 / 0 | 25.54 |
| four breaches | 8,444 / 16,888 | 42,204 / 8,444 / 8,444 | 1 / 4 | 25.53 |
| Door/repressurisation | 8,513 / 17,026 | 42,693 / 8,578 / 8,448 | 2 / 1 | 25.83 |
| many Sectors | 9,856 / 19,712 | 52,560 / 11,264 / 8,448 | 128 / 0 | 28.94 |
| tall/offset | 7,872 / 15,744 | 39,504 / 8,768 / 6,976 | 2 / 0 | 24.75 |

All also have 4,096 gas slots. Corresponding Geometry C++ residency is roughly
7.8–8.5 MiB; raw byte counts include allocator usable-block rounding. The largest
measured candidate live/build peak is **30,349,016 bytes** (many Sectors).
These are integrated measurements, not P1's isolated ~4–5 MiB estimate and not
proven maxima for arbitrary geometry. No dense surface matrix is introduced.

Accepted candidate rows use **66–996 thermal work units**, **1–11 intervals**,
**2–31 nonlinear iterations**, **12–180 linear iterations**, and no refinements.
The 0.1 s repressurisation row alone uses 710 work units / 11 nonlinear /
122 linear iterations. Eleven intervals in some 1 s rows reflect the existing
floating-point remainder subdivision, not dropped time or an increased source
interval. Failed-attempt iteration counts are intentionally not substituted
with the last committed counts.

Largest accepted thermal energy residual magnitude is **1.615e-9 J**; largest
nonlinear residual is **2.747e-7 J** (the large cold repressurisation fixture,
with the solver's separate scaled nonlinear criterion). Cumulative global
residuals are not identical to those thermal residuals: maximum absolute global
residual is **8.291e-4 J** in the controlled drain after about **1.253e10 J**
of cumulative inventory/path ledger magnitude. Ordinary loaded 0.1 s global
residual is **3.910e-6 J**. These raw whole-model accumulation/cancellation values
are explicitly disclosed, not claimed to satisfy a fixed absolute 1e-7 J global
bound or used to relax any production residual limit. Physical-default/global
account precision review remains part of the pending owner decision.

### Physical, compatibility and UI validation

- Full incremental **Release build succeeded**.
- `ctest --test-dir build-linux -C Release --output-on-failure -j 16`:
  **141/141 passed**, **164.07 seconds**. Includes both new tests; full standalone
  compartment/scenario, schema, calibration, API, sparse solver, geometry,
  observation, S0, repressurisation, rollback/allocation and Editor suites remain
  green. This does not turn the large-matrix refused attempt into a success.
- [Acceptance traceability](structural-acceptance.md) retains the precise
  component/physical contracts and distinctions between automation and review.
  Candidate calibration remains within its tested 5% anchors; it is not material
  certification or C1 default approval.
- Headless actual-panel/grid-colour CPU samples are **0.68–1.63 ms** for candidate
  geometries. The separate authored-preset Editor update includes all-Sector
  history publication; raw values are retained rather than subtracted from
  unmatched controlled-state facade measurements. Full interactive/GPU review,
  complete grid drawing and long populated histories remain open, not zero-cost.
- Geometry/defaults, standalone behaviour, model/preset revisions, work limits,
  YAML, numerical error bounds and fidelity are unchanged. #20/#21 remain open.
- Whitespace checks pass. No claim of a passed rollout gate is made.

## Subsequent gas-solver accuracy audit

The owner's two-Door S0 investigation found a separate spatial-fidelity concern:
[gas-solver correctness audit](cell-fluid-correctness-audit.md). Independent HLLE
flux, conservation, symmetry and activity checks pass, but numerical momentum
diffusion dominates local advection and corner pressure/load estimates are
strongly resolution-dependent. This is not a validated physical stagnation result
or a passed force-prediction accuracy gate. No production solver change was made;
142/142 Release tests pass with the added audit test. The existing rollout
measurements above remain historical evidence, not accuracy approval.

## Requested rollout decision

**Measurement recommendation: no-go for production default enablement; retain
Geometry as the reusable API default.** The subsequent owner-requested Editor
startup selection is for personal testing, not a passed performance gate. Performance misses and any accelerated refusal must
be addressed or explicitly scoped by the owner, not hidden by parallelisation,
a larger work limit, a looser energy bound or automatic backend substitution.

Owner decision is pending. Options include accepting a no-rollout outcome and
closing #35 as a reviewed no-go, or commissioning scoped serial optimization/
accuracy research before rerunning this gate. Changing the budget/accuracy/scope
would require an explicit parent review/amendment. No approval is inferred from
assignment or from functional tests passing. Interactive/full-GUI review and C1
physical-default approval are also still outstanding.
