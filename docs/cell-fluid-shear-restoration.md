# Cell-scale shear restoration — native gas revision v2

**Implemented:** `cell-fluid-v2-shear`. This changes native simulation behaviour,
not just the arrows or diagnostics. The owner requested plausible flow at the
existing 2 m Cell granularity after the
[HLLE audit](cell-fluid-correctness-audit.md) found excessive sideways momentum
mixing in the S1–S0–exterior replay. The goal is useful qualitative jets, filling
and load patterns, not 100% CFD accuracy or certified object-force predictions.

Both native Geometry and LocalStructure thermal backends use the revised gas
flux. The standalone well-mixed compartment runtime remains unchanged. No new
backend selection, save schema, authored geometry, material default, gas EOS,
force formula, acoustic speed, physical time or work limit was introduced.
[ADR-0004](adr/0004-cell-atmosphere-and-horizontal-momentum.md) records the numerical
amendment. Existing rollout performance/physical-default approval is separate.

## What changed

Keep the existing first-order HLLE bulk flux, but restore its linearly-degenerate
**shear wave** at the contact speed. Previously, a jet's tangential momentum could
be diffused sideways at the much larger sound-speed scale. The surrounding
pressure/Wall response then produced conspicuous adverse corner loads. This
correction changes the gas pressure and velocity field itself; force arrows still
use their original drag plus pressure-gradient calculation.

Full HLLC was not selected: it also restores density/composition contacts, which
would remove the current equal-pressure composition mixing and require a broader
explicit mixing-model decision. The targeted shear correction preserves that
contract and leaves normal acoustic/shock dissipation intact. It adds no fitted
attraction toward an opening, directional sign rule or momentum damping constant.

### Conservative characteristic correction

For an oriented open face, let `sL < 0 < sR` be the existing HLLE wave bounds,
`u` the Roe-weighted normal velocity and `vt` the Roe-weighted global tangential
velocity. With `rL = sqrt(rhoL)`, `rR = sqrt(rhoR)`:

```text
u  = (rL*uL  + rR*uR ) / (rL+rR)
vt = (rL*vtL + rR*vtR) / (rL+rR)
S  = rL*rR*(vtR-vtL)
delta = max(0, 1 - max(u,0)/sR - min(u,0)/sL)
C = -sL*sR/(sR-sL) * delta * S
F_tangent += C
F_energy  += vt*C
```

`S` is the shear characteristic strength, equivalently
`delta(momentum_tangent) - vt*delta(density)`. The factor reduces the shear-wave
HLLE viscosity to contact-speed upwinding. For an exact stationary shear interface
(equal pressure, density and zero normal velocity), the transverse momentum and
energy fluxes are zero, rather than acoustic-speed mixing. A moving shear wave
uses the physical upwind/contact velocity. It does not cap or slow sound waves.

The same corrected flux is booked once with opposite signs at the two endpoints,
including its kinetic-energy contribution. Species, normal momentum and bulk
energy transport are not independently clamped or rebalanced. Exterior and
unresolved-gas faces retain the existing treatment; supersonic upwind faces need
no correction. Equal tangential velocities take a fast zero-correction path.

### Admissibility and numerical safety

Construct the two modified HLL fan states from the original HLL state and
`correction/sL`, `correction/sR`. Apply the correction only if both retain finite,
nonnegative internal energy and positive density. Otherwise keep that face's
original conservative HLLE flux. This is a deterministic mathematical limiter,
not a slow-hardware fidelity switch or a temperature/inventory clamp.

Whole-Cell positivity checks, bounded time-step refinement and whole-call
work-budget refusal/rollback still apply. Native stepping still allocates no
successful-step heap memory. Species/mass, total gas energy and momentum/Wall/
exterior bookkeeping remain conserved to the existing tested tolerances.

Exact square roots of current density are cached in a Reset-allocated per-Cell
vector rather than recomputed on each face. Refresh, species addition/removal,
condensation rollback and state restoration keep it synchronized. Heat-only
updates do not change density. This derived cache adds approximately **32 KiB for
4,096 Cells**, not stored heat, gas capacity or a new conserved state. Source and
restore equivalence tests compare it against fresh construction.

## Before/after: owner's portable S0

Use the unchanged portable owner fixture, exterior Door open at Reset, S1–S0
Door opened at 1 simulated second, 0.1 s host updates, LocalStructure. Coordinates
are zero-based. At 1.1 s:

| Quantity | Original HLLE | Shear-resolving v2 |
|---|---:|---:|
| Inlet `(13,7)` wind X, m/s | 28.72 | 48.91 |
| Below-inlet `(13,8)` wind X, m/s | 7.57 | 0.95 |
| `(13,9)` resultant X-load, N | -51.30 | +10.01 |
| `(14,9)` resultant X-load, N | -91.08 | +7.53 |
| `(15,9)` resultant X-load, N | -39.72 | -2.47 |
| `(13,8)` resultant Y-load, N | +418.80 | +156.56 |

The inlet jet is substantially more directional instead of its momentum being
smeared into neighbouring rows. The large sustained adverse bottom-row pattern
is removed in this replay. At 1.2 s the three bottom-row X-loads are approximately
+10.67, +13.47 and +2.80 N. At 2 s they remain approximately +6.15, +15.05 and
+8.89 N. This is not an assertion that every instantaneous force must point
straight toward a Door: a weak transient corner counter-force still appears at
1.1 s, and off-jet gas can move downwards as a room fills.

The [paired owner samples](benchmarks/cell-fluid-shear-owner.csv) retain pressure,
wind and both resultant components, not just the favourable rows. These are
model observations, not experimental force calibration. Opening delays/speed,
other controls and initial loads can change the trajectory.

## Regression and safety validation

- **Red before fix:** the exact stationary/moving shear transport test fails with
  `Acoustic-speed numerical diffusion smears a contact-speed shear wave` against
  the original solver. Both axes and normal speeds 0, ±20 and ±600 m/s are tested.
  With v2 the flux is correctly stationary/contact-upwind, including energy.
- An independent full-vector characteristic-correction oracle agrees with the
  optimized implementation. Reflecting-Wall fluxes continue to use the original
  mirrored-state HLLE oracle.
- Two-Door mass/energy/momentum accounting, reflection/axis/face-orientation
  symmetry, full-evaluation versus activity shortcuts, and source/cache/restore
  equivalence pass.
- Eight deterministic 8×8 strong-shear/shock cases cover densities 1e-9–1e2 kg/m³,
  temperatures from 0 K to about 3000 K, velocities up to ±1000 m/s, empty Cells,
  twenty updates each, conservation/positivity and exact refused-inventory
  retention. This is stress coverage, not proof for every possible input.
- The portable S0 test now includes the actual two-Door sequence at 2 m: a
  directional inlet jet with over 10× the off-jet X-speed and, after 0.2 s of
  filling, bottom-row forces no longer dominated by adverse numerical pressure.
  The same test linked to the original gas implementation fails at the jet
  check. These are specific qualitative replay expectations, not a universal
  force-direction constraint in the solver.
- Existing composition mixing, acoustic propagation, shocks/reflections,
  prolonged evacuation, zero-allocation success, source/phase heat bookkeeping,
  thermal energy closure, whole-call 1x/10x rollback, schema and standalone tests
  remain part of the full suite.

Full incremental **Release build succeeded**; **142/142 CTest tests passed** with
`-j 16` in **162.27 seconds**. The existing correctness and S0 tests were expanded;
no CTest count increase is implied.

## Serial cost and limitations

Paired Release serial runs on the documented AMD RYZEN AI MAX+ 395 development
host, GCC 15.2.0 `-O3 -DNDEBUG`, pinned to logical CPU 4, unchanged boost/governor:
three fresh measured trials plus two discarded warmups per integrated row. The
baseline binary was copied before the production flux change; no builds/CTest
ran alongside either measurement. Same 44-row matrix, Wind/forces enabled,
unchanged 4096 acoustic/thermal budgets. The two runs are development samples,
not portable performance guarantees. Raw
[before](benchmarks/cell-fluid-shear-before.csv) and
[after](benchmarks/cell-fluid-shear-after.csv) CSVs retain construction, update,
publication, representative headless GUI, work, memory and refusals.

Representative full-facade times, normalized ms per 0.1 simulated seconds:

| Backend / scenario | Call duration s | Before | After |
|---|---:|---:|---:|
| Geometry, four breaches | 0.1 | 7.47 | 10.19 |
| Geometry, sustained breaches | 0.1 | 10.48 | 16.94 |
| Geometry, sustained breaches | 1 | 9.93 | 17.96 |
| Geometry, fully evacuated | 0.1 | 0.47 | 0.47 |
| LocalStructure, four breaches | 0.1 | 33.57 | 35.67 |
| LocalStructure, sustained breaches | 0.1 | 36.47 | 42.23 |
| LocalStructure, sustained breaches | 1 | 36.16 | 43.44 |
| LocalStructure, fully evacuated | 0.1 | 19.67 | 19.61 |
| LocalStructure, cold repressurisation | 0.1 | 174.17 | 192.96 |

**The quality improvement costs CPU time.** Do not advertise this as a speedup
or a passed ≤10 ms gate. Acoustic steps can also change because the revised jet
has different physical velocities; sound speed/time/budgets were not capped.
43/44 calls accept in both runs. The 1-second LocalStructure cold-repressurisation
case still refuses at its existing thermal work budget; the after-run attempt
cost is 1086.09 ms, not accepted throughput. Its starting time and committed solid
energy remain retained by the matrix checks.

The [kernel-only before](benchmarks/cell-fluid-shear-kernel-before.txt) and
[after](benchmarks/cell-fluid-shear-kernel-after.txt) studies use ten measured
trials plus two warmups, construction excluded. Sustained four-breach work costs
9.69 → 16.83 ms per 0.1 s (57 → 59 acoustic substeps); the fully disturbed case
costs 7.47 → 12.45 ms at the same 44 substeps. Those samples isolate the extra
numerical work rather than structural cost. Density-root caching and shared
reciprocals reduce redundant calculations; they do not skip active regions.
Integrated candidate C++ live/build peak is 30,381,408 bytes (excluding Lua/direct
malloc and allocator overhead, as before); the final process peak RSS is 48,936
KiB. C++ residency and process RSS have different accounting boundaries.

The existing rollout gate remains open/unpassed. This owner-requested change is
available for hands-on testing; broader performance/physical-default approval
has not been inferred from the request for plausible flow.

This remains an approximate, first-order, Cell-scale compressible model. The
restricted-aperture/Wall policy remains a prototype rather than resolved nozzle
flow; no turbulence, object obstruction/trajectory or sub-Cell model was added.
Wind and reference loads remain qualitative engineering estimates. The change
improves the observed behaviour, not the claim of absolute physical accuracy or
performance. Normal acoustic speeds, budgets and fidelity are never reduced to
make a case appear faster.

## Try it

Restart the rebuilt Release Editor and Reset the simulation. The inspector shows
**Gas model: cell-fluid-v2-shear**; LocalStructure still starts selected for the
owner's testing, and Geometry remains available. The numerical change affects
both native thermal backends. A previously running process continues using its
old code until restarted; Reset alone cannot hot-swap a compiled gas algorithm.

```sh
cmake --build build-linux --config Release -j 16
ctest --test-dir build-linux -C Release -j 16 --output-on-failure
build-linux/station-systems/tests/station-cell-fluid-correctness
build-linux/systems-bridge/nsi-structural-s0-test \
  systems-bridge/tests/fixtures/owner-station-s0.yaml
# Run serial measurements alone, not alongside a build/CTest.
taskset -c 4 build-linux/editor/editor-integrated-thermal-benchmark 3
```
