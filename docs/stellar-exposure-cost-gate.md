# Stellar exposure serial cost gate (#42)

**Gate not passed. The orbital exposure model is measurably not free, and it is
not what broke the gate.** Every row in this matrix exceeds the standing
≤10 ms / 0.1 simulated-second rollout target from
[#35](integrated-thermal-rollout.md) — but so does the dark-sky control that
has no exposure model at all, at 24.1 ms. The exposure feature adds cost on
top of a baseline that was already over target, and this report asks for the
same owner decision #35 is still waiting on rather than absorbing the delta.

This is the verification-and-measurement slice of the
[#36](https://github.com/ajare/numina-space-industries/issues/36) orbital
stellar exposure chain. It proves the feature against the repo's standing
numerical contracts (allocation, determinism, refusal rollback) and records
what it costs. It changes no physics, no work budget and no default.

## Reproduce

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --config Release -j 16
# Run alone, not concurrently with builds/CTest.
/usr/bin/time -v taskset -c 4 \
  build-linux/editor/editor-stellar-cost-benchmark 5 64 \
  > stellar-exposure-cost-gate.csv
ctest --test-dir build-linux -C Release --output-on-failure -j 16
```

`editor/tests/StellarCostBenchmark.cpp` is Linux-only evidence tooling
(`malloc_usable_size` accounting), not a portable public API and not a
wall-clock CTest assertion. Its `1 8` invocation is a small functional CTest
smoke row (`editor-stellar-cost-smoke`) that keeps the tool honest without
putting a three-minute matrix inside the suite.

### Hardware, build and revisions

Development host: AMD RYZEN AI MAX+ 395 w/ Radeon 8060S
(16 cores/32 threads, 64 MiB L3), 123 GiB RAM,
Linux 7.0.0-38-generic x86-64, GCC 15.2.0, CMake 4.2.3, CMake Release
`-O3 -DNDEBUG`, no special architecture flags/LTO. One serial process pinned
to logical CPU 4; boost enabled and frequency/governor unchanged. No
parallel numerical work. Source is `dbc99a2` (#41) plus the #42 commit
containing this report and tooling.

Output records gas model `cell-fluid-v2-shear`, save schema 4, Wind
diagnostics and reference-object forces enabled, and the unchanged acoustic
and thermal whole-call budgets of 4096.

Each single-update row averages five fresh constructions/attempts after two
discarded warmups. The whole-orbit row is its own sample set — see below.
Max update cost is the largest measured attempt, not a statistical bound.

## Fixture and the four skies

Every row runs the **same** sealed 64×64 `MultiLayerBlanket` /
`InsulatedPartition` fixture the #35 gate ran: 4,096 active Interior floor
Cells, 8,448 exterior Structural contacts, 26.35 MiB live C++ after
construction. **The only thing that varies between rows is the authored sky.**
A row minus the dark-sky `rest` row at the same duration is therefore the
measured cost of the exposure model itself rather than an assumed one.

| Scenario | Authored sky |
|---|---|
| `rest` | No `Orbit` at all — dark sky, zero stellar term, evaluator never built. The pre-feature control. |
| `sustained-sunlit` | `Orbit` at 80° axial tilt, past the 69.7° shadow angle for this radius: never eclipsed, direct plus reflected light the whole cycle. |
| `eclipse-transition` | `Orbit` at 0° axial tilt, walked (untimed) to the eclipse-entry instant so the timed window straddles the crossing. |
| `full-orbit` | `Orbit` at 0° axial tilt, one complete cycle walked from Local noon: 600 updates at 1x, 60 at 10x. |
| `rest-recheck` | The `rest` control measured again at the end of the matrix. Not a fifth sky — the host-drift control. |

The cycle is **compressed to a 60 s Orbital period**. Orbital period and
orbital radius are independent authored inputs (ADR-0008: no Kepler, no
gravity model), so shortening the period leaves the ISS-radius eclipse
fraction — 38.7%, both crossings, and the whole shape of the exposure
profile — exactly as a real LEO orbit has it, while putting a complete cycle
inside a measurement budget instead of an hour of simulated clock. The
compressed cycle therefore exercises the same exposure profile at a higher
thermal-cycling rate than a real 92.8-minute orbit would; it is a cost
fixture, not a fidelity claim.

**Durations are 0.1 s (1x) and 1.0 s (10x)**, the Editor's own
`SimulationPlayback::speeds` endpoints, and each is a single host call — not
ten GUI frames.

### Timing boundaries

- `construction_ms`: expansion, scratch/rollback/index allocations, runtime
  construction and initial snapshot. Fixture YAML creation is outside.
- `numerical_ms`: the facade's own timing around the native transport call —
  the numerical work, including committed-state aggregation.
- `lifecycle_ms`: the lifecycle sandbox prepare, measured in that same update.
- `publication_remainder_ms`: whole update minus those two stages. Includes
  snapshot publication, Wind/force diagnostics, commit and bookkeeping. Not
  a pure snapshot-copy microbenchmark. Refused rows report `NA`, never a stale
  sample.
- `gui_cpu_ms`: real headless ImGui/ImPlot selected-Sector inspector, bounded
  structural topology and histories, the **Stellar exposure trace and its
  Eclipse bands**, the timing panel, plus Level-0 grid colour generation (not
  grid drawing). Two warmup frames then five timed frames, 1400×2400 display,
  1200×2200 panel.
- `gui_host_update_ms`: one separate `SimulationPlayback` update on the
  authored-preset state including facade publication and all-Sector history
  aggregation. Not additive to the main row.
- `exposure_step_us`: **the per-source-slice cost of the exposure evaluator
  itself**, measured in isolation on the fixture's own 8,448-face exterior
  set — one `evaluate()` plus the per-face effective-sink, direct and
  reflected application the transport performs. The dark row runs the same
  loop against a dark evaluator, so the lit-minus-dark delta is the orbital
  arithmetic rather than the loop.
- `exposure_update_us`: `exposure_step_us` × source slices in the update.
  The source-slice cap is 0.1 s, so a 10x update pays it ten times.
- `prepare_ms`: untimed preparation walked through the real runtime. The
  eclipse-transition row walks ~18 simulated seconds and is the only row
  where this is non-zero. It gets its own generous work budget because it is a
  starting condition, not a throughput claim; the measured window stays on
  the standing 4096 gate budgets.

SDL/OpenGL GPU submission, full grid draw, presentation/vsync, interactive
readability and saturated-history performance remain **unmeasured**. Headless
frames are not interactive rollout approval.

## Measured results

[Raw 10-row CSV](benchmarks/stellar-exposure-cost-gate.csv) and
[process resource report](benchmarks/stellar-exposure-cost-gate-time.txt)
are checked in. The run completed in **3:10.01**, peak RSS **43,164 KiB**.
All ten rows accepted; nothing refused, no work limit raised.

Times in ms unless the column says otherwise. `per 0.1s` normalises the whole
accepted update to 0.1 simulated seconds.

| Scenario | s | per 0.1s | over gate | vs rest | construction | numerical | lifecycle | publication | GUI CPU |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| rest | 0.1 | 24.146 | +14.146 | — | 26.482 | 23.792 | 0.172 | 0.037 | 1.403 |
| rest | 1 | 29.889 | +19.889 | — | 27.209 | 298.521 | 0.123 | 0.049 | 1.390 |
| sustained-sunlit | 0.1 | 24.500 | +14.500 | +0.353 | 26.778 | 24.213 | 0.116 | 0.034 | 1.360 |
| sustained-sunlit | 1 | 34.121 | +24.121 | +4.232 | 28.300 | 340.824 | 0.134 | 0.050 | 2.027 |
| eclipse-transition | 0.1 | 35.739 | +25.739 | +11.593 | 25.756 | 35.292 | 0.276 | 0.034 | 1.346 |
| eclipse-transition | 1 | 35.479 | +25.479 | +5.590 | 25.599 | 354.328 | 0.285 | 0.035 | 1.299 |
| full-orbit | 0.1 | 35.442 | +25.442 | +11.296 | 25.373 | 21084.5 | 77.786 | 103.060 | 1.306 |
| full-orbit | 1 | 35.253 | +25.253 | +5.364 | 24.939 | 21128.4 | 12.393 | 10.773 | 1.380 |
| rest-recheck | 0.1 | 23.384 | +13.384 | −0.762 | 24.905 | 23.160 | 0.107 | 0.024 | 1.351 |
| rest-recheck | 1 | 28.966 | +18.966 | −0.923 | 25.047 | 289.351 | 0.109 | 0.039 | 1.392 |

Full-orbit columns are whole-walk totals: 21.08 s of numerical work over 600
updates at 1x (35.1 ms/update) and 21.13 s over 60 updates at 10x.

### The drift control first

`rest-recheck` is the same dark-sky fixture as `rest`, measured again after
the whole matrix has run. It came back **−0.762 ms** at 1x and **−0.923 ms**
at 10x — about 3% of the row. That is the band this host drifts over a
three-minute run (governor, thermals, cache residency), and it is what every
`vs rest` delta below has to clear before it means anything. A cost matrix
without a repeated control cannot attribute its own deltas at all, so the
control is part of the matrix rather than an afterthought.

Against that band:

- **Sustained sunlit at 1x (+0.353 ms) is inside the drift band** and is
  indistinguishable from free.
- Sustained sunlit at 10x (+4.232 ms), eclipse transition (+11.593 / +5.590)
  and full orbit (+11.296 / +5.364) are all well outside it. Those are real.

### The evaluator is not where the cost is

`exposure_step_us` is 59.5–61.4 µs for the dark sky and 71.5–77.5 µs for a
lit one, over 8,448 exterior Structural faces. **The orbital arithmetic is
roughly 11–16 µs per 0.1 s source slice — about 0.05% of a 24 ms update.**
At 10x an update pays it ten times (≈0.72 ms), still about 2% of the row.
The evaluator is doing what ADR-0008 designed it to do: pure, allocation-free,
one call per slice.

### The extra time is not extra solver work

This is the finding worth reading twice. **The committed thermal work counts
are identical across every sky:**

| Duration | work units | nonlinear iters | linear iters | intervals |
|---|---:|---:|---:|---:|
| 0.1 s update | 89 | 3 | 15 | 1 |
| 1.0 s update | 906 | 31 | 150 | 11 |

Identical for `rest`, `sustained-sunlit`, `eclipse-transition`, and
`rest-recheck`. The full-orbit walk totals 53,400 work / 1,800 nonlinear /
9,000 linear over 600 updates — exactly 89 / 3 / 15 per update.

So the +11.6 ms the eclipse transition costs is **not** more iterations, not
more intervals, not more refinements (zero refinements anywhere). The same
numerical work takes more wall clock once the hull is under a sun and then a
shadow. The measurement does not identify the mechanism, and this report will
not guess one: candidate explanations (per-iteration arithmetic on the new
value ranges, subnormal handling in the radiative terms, cache residency
after a long preparation walk, host frequency under sustained load) are not
separated by these numbers. **Treated as an open observation for review, not
folded into a conclusion.**

What is *not* in question is that the sun is doing real thermal work: the
full-orbit walk absorbs 98.4 MJ of stellar energy and puts 76.3 MJ of it
into the hull solids. The `rest` control absorbs nothing and *loses* energy.

### Memory

Live C++ residency after construction is 26.35 MiB on every row, dark
or lit: the exposure model adds no resident state beyond the evaluator's
own handful of doubles and the rollback copy of the exterior staging.
Build-peak equals live on every row. Process peak RSS 43,164 KiB for the
whole matrix.

## Contract verification

The measurement is only meaningful if the contracts still hold. They do, and
one of them turned out not to be holding before this ticket fixed it.

### Allocation

`nsi-stellar-exposure` now walks a **complete simulated orbit** (600
successful 0.1 s steps at the compressed cycle) with allocation tracking on
for the whole walk and asserts zero heap allocations after construction. The
walk is checked to have crossed eclipse entry and eclipse exit exactly once
each, read off the committed stellar ledger rather than off the evaluator —
so it cannot pass by never having entered shadow.

### Determinism across the crossings

Two transports built from identical geometry and advanced over identical step
sequences are compared field by field after **every** step of a whole cycle:
gas conserved quantities, solid energies, every Sector ledger, the structural
paths, the committed heat rates, the observation stamp, the energy closure,
and the exposure-derived exterior boundary staging. At each of the two
crossing steps the test additionally asserts the staged exterior state
actually moved — so the comparison cannot pass by having crossed nothing.

### Refusal rollback — a real gap, now closed

**Before this ticket a refused step did not restore the exposure-derived
state.** `CellAtmosphereTransport::advance` rolled back gas, solids, the
ledgers, momentum and the clock, but `structuralExterior_` — the per-exterior-
contact effective Deep-space sink temperature and absorbed stellar watts that
the orbital model stages on every source slice — kept the values written by
the attempt that had just been thrown away. A refused step left the committed
model described by a sky it was not standing under.

It is harmless to today's *behaviour* only because the staging is rewritten
in full before every solve. It is not harmless to the contract, and it would
turn into a live defect the moment anything cached across steps or read the
staged sink temperature as committed state.

The fix stages a copy of the exterior boundary state at the start of each
`advance` and restores it in the refusal path, alongside the solid
checkpoint. It is sized once at construction, so the successful path still
allocates nothing.

`refusalRestoresExposureDerivedState` lands the transport exactly on
eclipse entry, so the committed staging is the last sunlit sky and the
refused attempt's first source slice opens on the far side of the boundary
in shadow. It then asserts the refused duration was never committed to the
clock, gas/solids/ledgers/paths/energy closure are bit-identical, the last
committed heat rates and observation stamp are untouched, and the staged
exterior boundary state is bit-identical to the committed one — including
that faces which were absorbing stellar power still are.

This test was verified against a build with the restore line removed: it
fails with *"A refused step left the refused attempt's sky staged on the
hull boundary"*. It is not vacuous.

## Gate verdict

**Every accepted row is `over-target-needs-review` against the standing
≤10 ms / 0.1 simulated-second gate.** The CSV carries the verdict per row
rather than leaving it to be eyeballed.

| Scenario | 1x verdict | 10x verdict |
|---|---|---|
| rest (dark control) | over by 14.146 ms | over by 19.889 ms |
| sustained-sunlit | over by 14.500 ms | over by 24.121 ms |
| eclipse-transition | over by 25.739 ms | over by 25.479 ms |
| full-orbit | over by 25.442 ms | over by 25.253 ms |

**This is a regression report, not an absorption.** Two things must not be
conflated:

1. **The gate was already failing before this feature.** The dark-sky control
   — no orbit, no evaluator, no stellar term — runs 24.1 ms / 0.1 s, which
   is the pre-existing LocalStructure cost recorded in
   [#35](integrated-thermal-rollout.md) (23.542 ms there, 24.1 ms here) and
   2.4× the target on its own. The exposure model did not create this
   failure.
2. **The exposure model is nonetheless not free.** Under a lit or eclipsed
   sky the same fixture costs 11.3–11.6 ms / 0.1 s more at 1x and 4.2–5.6 ms
   more at 10x than the identical dark fixture, well outside the ±1 ms drift
   band. The worst case measured here is the eclipse transition at 1x:
   **35.7 ms per 0.1 simulated seconds, 3.6× the target.**

Neither of those is a reason to wave the delta through, and neither is a
reason to stop the feature. The decision belongs to the same pending owner
review as #35: the exposure chain is functionally complete and contract-clean,
and its serial cost is now measured rather than assumed. Changing the budget,
the accuracy target or the serial policy to make these numbers pass would
require that explicit review, not inference from this ticket.

## Validation

- Full incremental **Release build** succeeded (`build_incremental.sh
  --config Release`).
- `ctest --test-dir build-linux -C Release --output-on-failure -j 16`:
  **145/145 passed**, **162.22 seconds**. That is the 144 rows #41 left
  behind plus the new `editor-stellar-cost-smoke` row, and it includes the
  three new `nsi-stellar-exposure` contract cases (full-orbit allocation,
  eclipse-crossing determinism, refusal rollback of the exposure-derived
  state).
- No trailing whitespace in the changed files.
- `station-systems` is untouched: `NativeThermalExterior` still has exactly
  its two fields, and the accepted solver, its work budgets and its residual
  reporting are undisturbed.
- No physics, default, work limit, tolerance or wire format changed. The only
  production change is the refusal-path restore in
  `systems-bridge/src/CellAtmosphere.cpp` and a read-only accessor for the
  staged exterior state.
