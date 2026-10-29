# Serial structural/cavity cost review — P1 (#25)

Subsequent integrated measurements (not P1 estimates) are in the
[#35 rollout review](integrated-thermal-rollout.md). That gate is not passed;
Geometry remains the default.

**Prototype/cost gate only. No production backend, defaults, limits, YAML or
standalone compartment behavior changes.** Read with [#22](https://github.com/ajare/numina-space-industries/issues/22),
[ADR-0007](adr/0007-native-structural-thermal-contract.md) and
[candidate material calibration C1](thermal-calibration.md). #23 is closed; C1
from #24 is available, but remains provisional. This review supplies evidence for
#26/#28, not permission to enable a new backend. Default rollout still needs #35.

## Reproduce and measurement conditions

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --config Release -j 16
# Run the benchmark alone, NOT concurrently with CTest/builds.
/usr/bin/time -v taskset -c 4 \
  build-linux/editor/editor-structural-cost-benchmark 5 > structural-cost-p1.csv
ctest --test-dir build-linux -C Release -j 16 --output-on-failure
```

Recorded on Linux 7.0.0-38-generic, AMD RYZEN AI MAX+ 395 (16 cores/32 threads,
64 MiB L3), 122 GiB RAM, GCC 15.2.0, CMake Release `-O3 -DNDEBUG`, no LTO or
special architecture flags. One benchmark process pinned to logical CPU 4;
no parallel numerical work, CPU governor/boost unchanged (not fixed-frequency).
Five independent measured fresh constructions per row, two discarded warmups;
means and maximum update times are in the checked-in
[raw CSV](benchmarks/structural-cost-p1.csv). This is a development-machine
sample, not a confidence bound or portable performance guarantee. The complete
process took 12.03 s, peak RSS 40,012 KiB (includes fixtures, Lua, GUI contexts,
all sequential candidates and allocator retention; **not** a per-model footprint).

`editor/tests/StructuralCostBenchmark.cpp` runs the unchanged production
`CellPlaybackRuntime` with **both Wind diagnostics and reference-object force
calculations enabled**, plus the test-local P1 experiment in
`systems-bridge/tests/StructuralCostModel.h`. Selection is by constructing a
separate experiment, never a mid-run backend swap. Diagnostic identifiers are
`legacy-native` vs `P1-*`, material `C1-provisional`, save schema 4. P1/C1 are study
identifiers, not installed production model/preset revisions.

### Timing boundaries

- `build_ms`: Reset-equivalent runtime/model construction, including expansion
  and scratch allocation. Fixture YAML creation/loading and font-atlas creation
  are outside this timer. P1's temporary geometry maps are included and released
  at construction completion. No numerical update allocates scratch.
- Baseline `solver_ms` and `lifecycle_ms`: the existing successful-update timing
  sample, not a separate run; solver includes its internal Sector aggregation.
- `publication_remainder_ms`: total update minus those two measured stages.
  Includes snapshot publication, aerodynamic diagnostic calculation and small
  commit/bookkeeping/timer costs. It is **not** a pure publication microbenchmark.
  No copying of `snapshot()` is included.
- `gui_cpu_ms`: separate headless ImGui/ImPlot frame generation/render-list build
  for one selected Sector's thermal/atmosphere inspector (empty histories) and
  timing chart, 1200×1800 display, 1000×1500 panel. This exercises real panel code,
  but excludes grid rendering, populated history plots, SDL/OpenGL/GPU draw,
  presentation/vsync and the rest of the Editor. Full interactive frame/GPU cost
  is **unmeasured**, not zero. GUI time is not added to the numerical gate.
- P1 `update_ms` is **isolated thermal work**, including nonlinear residual audit;
  no acoustic transport, sources, phase/Lua, structural snapshot or structural GUI
  is implemented. `NA` is unmeasured/not applicable. Never add the old geometry
  heat model to P1 as a playback implementation. The old model must be replaced.
- `per_0.1s_ms = update_ms × 0.1 / seconds`: absolute normalized cost, not a
  speedup ratio. The 1-second row is a single 10x-duration call, not ten host calls.

## Fixtures and geometry/count checks

All fixtures have **4,096 active Interior floor Cells**. Generated YAML is
portable and passes real core geometry validation; no home-directory fixture.

- `rest`: closed 64×64, default MultiLayerBlanket / InsulatedPartition, 293.15 K.
  Normal cooling starts immediately; this is not an adiabatic rest measurement.
- `loaded`: same geometry, 4,096 occupants, per-person O2 use 1e-5 kg/s,
  CO2 7e-6 kg/s, moisture 1e-6 kg/s and 100 W sensible heat in the baseline.
- `breaches`: four full horizontal exterior openings at the corners (6 m² each).
- `sustained-breaches`: baseline preconditioned for one simulated second before
  the timed update. Preconditioning is excluded from construction/update times.
- `evacuated`: baseline uses an explicit 1e9 kg/s aggregate inventory leak and a
  one-second untimed preparation. Every primitive is checked unresolved before
  timing. This is artificial complete evacuation, not a claim that a physical
  breach empties the room in one second. Inventory removal remains ledgered.
  P1 starts gas-free but retains all structural capacity and radiation.
- `many-sectors`: 128 closed 4×8-Cell rooms on 64×64, the current gas-compartment
  limit, not an increased limit. Shared partitions are deduplicated.
- `tall-offset`: 32×64 lower floor with height two, an empty intermediate Level,
  and a 32×64 upper floor at Level 2 offset by (16,8) Cells. Thus 6,144 authored
  grid positions, but 4,096 active floor Cells and 6,144 claimed-volume voxels.
  Overlap is 896 floor patches, counted once, not two masses. Lower side Walls
  cover both height slices; the empty intermediate Level does not create holes.
- `gradient`: P1 gas in the left half starts at 313.15 K, other gas/solids at
  293.15 K. The unchanged playback facade has no initial-state injection API;
  **no corresponding baseline snapshot row is claimed**.

P1 expands world-coordinate occupied volume at existing Cell/Level boundaries.
One two-side panel is emitted per physical solid boundary; horizontal shared
floor/ceiling is emitted once even across offsets and tall claims. No surface
matrix is built. The flat case has `2×4096 + 4×64 = 8448` panels; the tall case
has `4×2048 - 896 + 576 = 7872`. Tests check the equivalent 16-wide cases,
including the exact 600-panel tall/offset count. Bodies include 4,096 gas slots
(also retained as zero-capacity placeholders in the vacuum experiment) and two
finite-capacity nodes per panel. Cavities follow geometric Interior Sectors.

## Current default baseline

Times in ms; `pub` is the publication remainder defined above. Full-duration
cost is shown alongside the normalized 0.1 s cost. Old thermal iteration counts,
acoustic steps and maxima are in the CSV.

| Scenario | Simulated s | Build | Solver | Lifecycle | Pub | GUI CPU | Update | /0.1 s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| rest | 0.1 | 15.581 | 0.414 | 0.102 | 0.110 | 0.093 | 0.626 | 0.626 |
| rest | 1 | 15.793 | 70.528 | 0.102 | 0.189 | 0.148 | 70.819 | 7.082 |
| loaded | 0.1 | 15.580 | 1.333 | 0.099 | 0.111 | 0.090 | 1.543 | 1.543 |
| loaded | 1 | 15.864 | 79.870 | 0.109 | 0.191 | 0.165 | 80.170 | 8.017 |
| breaches | 0.1 | 15.683 | 7.043 | 0.100 | 0.160 | 0.101 | 7.304 | 7.304 |
| breaches | 1 | 15.777 | 95.822 | 0.102 | 0.178 | 0.143 | 96.102 | 9.610 |
| sustained-breaches | 0.1 | 15.798 | 9.933 | 0.174 | 0.172 | 0.157 | **10.279** | **10.279** |
| sustained-breaches | 1 | 15.855 | 97.868 | 0.139 | 0.175 | 0.170 | 98.182 | 9.818 |
| evacuated | 0.1 | 15.567 | 0.296 | 0.091 | 0.045 | 0.091 | 0.431 | 0.431 |
| evacuated | 1 | 16.057 | 2.850 | 0.094 | 0.050 | 0.100 | 2.994 | 0.299 |
| many-sectors | 0.1 | 22.151 | 0.763 | 3.863 | 0.116 | 0.740 | 4.742 | 4.742 |
| many-sectors | 1 | 22.106 | 51.392 | 3.961 | 0.134 | 0.763 | 55.487 | 5.549 |
| tall-offset | 0.1 | 15.947 | 0.740 | 0.129 | 0.111 | 0.153 | 0.980 | 0.980 |
| tall-offset | 1 | 16.156 | 72.211 | 0.134 | 0.187 | 0.192 | 72.531 | 7.253 |

The previously quoted ~10.06 ms was historical, not headroom. This run reproduces
the marginal miss, at **10.279 ms**. Initial 0.1 s rest takes the resting acoustic
shortcut; after cooling creates gradients, the 1 s call does real transport (396
acoustic substeps). Do not extrapolate the first-row cost linearly. All durations
were accepted with the unchanged facade budgets. Many-Sector lifecycle cost is
visible and is not thermal solver time.

## P1 comparison: two-side, sparse local, algebraic cavity

Three ablations hold the same two-side capacities and panel discretization:

1. `P1-two-side`: positive through-panel links, normal-pressure gas films and
   local exterior radiation; no lateral links or interior cavity exchange.
2. `P1-local`: adds local in-plane/junction links where panels share an entire
   physical edge **and side cavity ownership**. No proximity links/isothermal bus.
3. `P1-cavity`: adds one diffuse algebraic field per Sector, including geometric
   enclosure in vacuum, and black exterior apertures for the four breaches.

For 0.1 s flat rest these cost **1.555 / 1.951 / 1.986 ms**, respectively. Persistent
storage is **3.942 / 4.691 / 4.691 MiB**. These are different heat paths, not three
physically equivalent solvers; the faster ablations are **not** fidelity fallbacks.

Each panel splits C1 total capacity evenly (Hull 400, partition 2000 J/(m² K));
solid R is 19.8930878347 or 3.25194391034 m² K/W. Interior epsilon=0.8, exterior
MLB epsilon=0.03, space=3 K, no solar input. Gas capacity is an explicitly
provisional dry-air proxy, `12 × height × 1.2 × 718 J/K`, separate from solids.
Normal gas films use the C1 reference value 19.9950194961 W/(m² K), vacuum films
are absent. **No dynamic rarefied-film evaluation or EOS replacement is claimed.**
For a simple bounded topology-cost experiment all same-side local routes use
C1's smaller Hull sheet coefficient 0.01 W/K per side and seam 0.05 W/(m K),
with two half-sheet resistances and one seam in series. Partition sheets are
not yet specialized to C1's larger value. These are provisional cost coefficients,
not new presets or evidence that partition transient calibration is complete.

### Matrix-free algorithm and work

Backward Euler uses intervals ≤0.1 simulated seconds. At each lagged nonlinear
iteration, positive radiation secants `epsilon A sigma (T+Tc)(T²+Tc²)` give a
symmetric passive linear network. Eliminate each cavity analytically via its
weighted scalar sum (including its exterior-aperture boundary weight), applying
the resulting diagonal-minus-rank-one operation **without materializing it**.
Jacobi-preconditioned CG solves temperature increments; exterior radiation and
cavity temperatures are iterated. This is a cost experiment, not the future
station-systems implementation.

P1 explicitly bounds each interval to 16 nonlinear iterations and each linear
solve to 256 CG iterations; calls accept only `(0,1]` seconds, hence at most ten
intervals and 40,960 CG iterations as an absolute study bound. Those are P1-only
bounds, **not changes to production work limits**. Measured successful work is
far smaller. Temperature-change convergence is 1e-9 K, linear max residual
1e-8 J. Original nonlinear flux equations are evaluated independently afterward.
There is no thermal subcycling tied to sound speed, parallelism, temperature
clamp or wall-clock refusal. P1 throws on a failed solve; it is not a transactional
playback backend and does not purport to implement production rollback.

| Scenario | Simulated s | Build ms | Thermal ms | /0.1 s ms | Bodies | Panels | Links | Cavities | MiB | Nonlinear / CG / matvec |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| rest | 0.1 | 4.331 | 1.986 | 1.986 | 20992 | 8448 | 50688 | 1 | 4.691 | 3 / 9 / 12 |
| rest | 1 | 4.425 | 20.426 | 2.043 | 20992 | 8448 | 50688 | 1 | 4.691 | 30 / 90 / 120 |
| loaded | 0.1 | 4.411 | 1.977 | 1.977 | 20992 | 8448 | 50688 | 1 | 4.691 | 3 / 9 / 12 |
| loaded | 1 | 4.370 | 20.061 | 2.006 | 20992 | 8448 | 50688 | 1 | 4.691 | 30 / 90 / 120 |
| breaches | 0.1 | 4.434 | 2.909 | 2.909 | 20984 | 8444 | 50648 | 1 | 4.691 | 4 / 16 / 20 |
| breaches | 1 | 4.471 | 23.595 | 2.359 | 20984 | 8444 | 50648 | 1 | 4.691 | 31 / 124 / 155 |
| sustained-breaches | 0.1 | 4.442 | 2.989 | 2.989 | 20984 | 8444 | 50648 | 1 | 4.691 | 4 / 16 / 20 |
| sustained-breaches | 1 | 4.465 | 24.117 | 2.412 | 20984 | 8444 | 50648 | 1 | 4.691 | 31 / 124 / 155 |
| evacuated | 0.1 | 4.402 | 1.872 | 1.872 | 20992 | 8448 | 42240 | 1 | 4.691 | 3 / 9 / 12 |
| evacuated | 1 | 4.397 | 18.697 | 1.870 | 20992 | 8448 | 42240 | 1 | 4.691 | 30 / 90 / 120 |
| many-sectors | 0.1 | 5.767 | 2.091 | 2.091 | 23808 | 9856 | 60544 | 128 | 4.890 | 3 / 9 / 12 |
| many-sectors | 1 | 5.922 | 21.003 | 2.100 | 23808 | 9856 | 60544 | 128 | 4.890 | 30 / 90 / 120 |
| tall-offset | 0.1 | 5.108 | 1.823 | 1.823 | 19840 | 7872 | 48128 | 2 | 3.987 | 3 / 9 / 12 |
| tall-offset | 1 | 5.185 | 18.792 | 1.879 | 19840 | 7872 | 48128 | 2 | 3.987 | 30 / 90 / 120 |
| gradient | 0.1 | 4.449 | 2.635 | 2.635 | 20992 | 8448 | 50688 | 1 | 4.691 | 3 / 15 / 18 |
| gradient | 1 | 4.567 | 32.898 | 3.290 | 20992 | 8448 | 50688 | 1 | 4.691 | 38 / 190 / 228 |

P1 does not advance gas transport/sources: `loaded` has the same P1 initial state
as rest; sustained-breach P1 rows likewise start with the same thermal state as
initial breaches. Their repeated rows are timing replicates, **not coupled load
or sustained-pressure physics validation**. Actual sustained Wind/loads are in
the baseline rows. A coupled new-backend benchmark belongs to #29/#35.

### Memory and topology envelope

`bytes` counts actual vector **capacity**, not just live elements: bodies,
links, surfaces, cavities and nine per-body double scratch vectors. It excludes
allocator headers, vector objects and temporary construction maps. No numerical
heap allocation is observed by the allocation-counting test. Flat P1-cavity has
8,448 through links, 8,448 films and 33,792 local links; evacuation removes films,
not solids. Many-Sector topology has 9,856 / 11,264 / 39,424 links respectively.
The table is a **measured envelope, not a guaranteed geometry maximum**; tall
height claims and future Door/frame splits affect counts. Peak construction
working memory must be budgeted separately from persistent vectors.

For comparison, the flat production baseline has 4,096 gas bodies plus one
shared Hull and 4,096 aggregate Cell/Hull contacts; these are not panel counts.
Baseline total resident model memory is not isolated here (`NA`), rather than
misrepresenting `sizeof(runtime)` as its heap footprint. A dense double matrix
for 16,896 flat surfaces alone would take ~2.13 GiB versus ~4.69 MiB persistent
P1 storage. Sparse links + scalar cavity operations are the viable direction.

## Accuracy findings and explicit budget decision

Automated `nsi-structural-cost` checks: exact panel/side counts, shared/tall floor
deduplication, vacuum retaining solids, finite cooling without gas, zero exchange
in a uniform closed cavity, 1.8 m² black-aperture bound, `hull:none`, no successful
update allocation and 0.1 vs ten 0.01 s steps (maximum temperature discrepancy
below **0.002 K**, 16-wide gradient fixtures). This is temporal consistency for
these mild fixtures, not validated physical accuracy or the parent acceptance
suite. All matrix rows are computed serially.

Independent P1 nonlinear node residuals in the recorded matrix are ≤5.51e-9 J;
worst summed energy closure is **1.01e-5 J**. The executable's explicit exploratory
checks are 1e-5 J/node and 1e-4 J/global. **The latter is looser than the production
`1e-7 J + 1e-12 × sum(abs(applied node heat))` contract and is not an amendment to
it.** Double temperature/inventory cancellation across ~20k bodies is visible.
Before adoption, #28 must use adequately precise inventory/increment bookkeeping,
converged nonlinear balances and the contracted residual acceptance, including
refinement and refusal. No temperature clamp or hidden capacity is acceptable.

**Recommendation:** retain two finite-capacity sides per physical panel, explicit
finite local sparse links and matrix-free algebraic Sector cavities for the next
API/solver prototype. The measured ~20–24k-body / ≤61k-link cases fit comfortably
in a few MiB of persistent scratch and ~4–6 ms Reset construction. Do not replace
local nodes with a global isothermal Hull, drop cavity physics in vacuum, or use
the faster ablations automatically. Gas and structural observations stay distinct.

**Performance decision:** the ≤10 ms target is **not met by the sustained baseline
and not demonstrated for a combined new backend**. The additional isolated thermal
cost is ~1.8–3.3 ms per 0.1 s. A conservative planning sum of sustained baseline
10.279 + initial-breach thermal 2.909 is ~13.19 ms, but this double-counts old
thermal work and is **not an integrated measurement**. Budget that serial margin
until #29 supplies replacement-backend measurements. No approval to relax accuracy,
change the 10 ms target or enable defaults is granted. Seek explicit parent review
of an absolute budget/accuracy tradeoff at #35 if integrated serial work still
misses; do not cap sound speed, increase existing limits or substitute fidelity.

### Remaining uncertainty for implementation/review gates

- Door leaves/frames and energy-preserving actuation, connected inter-Sector
  radiative portals, reflective singular components, and varying pressure/mixture
  films are not implemented by P1. Multiple cavities here are closed; breaches
  couple only to exterior. A connected cavity graph must use sparse scalar
  elimination/iteration or a coupled sparse block, not dense surface fill-in;
  its iteration/conditioning cost remains an explicit #28/#30/#35 risk.
- The P1 geometry expander is a test fixture, not stable production surface IDs,
  a complete Door topology builder or a checked-allocation contract. #27 must
  count before allocation, state independent structural limits and test height,
  offset, missing Partition and Interior Void-preset ownership comprehensively.
- Full pressure trajectories, repressurisation/chilled structure, original S0,
  tight energy residuals, rollback and worst-case accepted whole-call budgets
  remain acceptance work in #28–#35. No anonymous object capacity (#21) or
  parallelisation (#20) is introduced.
- Structural snapshot size/publication and full GUI/GPU work remain unmeasured
  until those observations exist. Budget and benchmark them, never assume zero.

Validation: full incremental Release build succeeded; **all 131 CTest tests
passed** with `ctest --test-dir build-linux -C Release -j 16 --output-on-failure`.

This completes the scoped reproducible prototype/cost review, not the structural
thermal implementation or rollout gate.
