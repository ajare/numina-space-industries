# Cell-fluid correctness audit: two-Door S0 corner loads

**Historical HLLE baseline audit (`45d170b`).** The owner subsequently requested
plausible flow at the existing Cell scale; [shear restoration v2](cell-fluid-shear-restoration.md)
now changes the production native gas flux and substantially improves this replay.
The findings and raw baseline data below are retained, not presented as results
from the revised solver. Reproduce these historical numbers from `45d170b`; the
current study executable uses v2.

## Verdict

**The coarse load/pressure predictions are not validated by the previous
stagnation explanation.** The reported away-pointing arrows reproduce in the gas
kernel alone. The audited HLLE implementation agrees with an independent
conservative-flux calculation, and the two-Door case passes conservation,
reflection/rotation, face-orientation and activity-shortcut checks. No sign,
double-booking or flux-algebra defect was found in those exercised paths.

However, **numerical momentum diffusion dominates local physical advection**, and
the corner pressure/load magnitudes change substantially with spatial resolution.
This is a demonstrated accuracy limitation of the current first-order,
restricted-aperture model, not evidence that the displayed forces are reliable
object-motion predictions. It does not prove every adverse gradient is false, or
that the finest diagnostic mesh is physically converged.

No production numerical algorithm, limits, error tolerances, wind/force display,
thermal backend, material or authored geometry was changed to conceal the symptom.
The new code is tests and an explicit research tool. Fixing/calibrating spatial
fidelity requires a separate numerical-method decision and regression/performance
review, not arrows forcibly pointing toward an opening.

## Reproduction and minimisation

The owner's `~/station.yaml` matched the checked-in portable
`systems-bridge/tests/fixtures/owner-station-s0.yaml` byte for byte (SHA-256
`41a69a1a8d14fe2b98fc308bc3f554ef084f936eda817ed715a1104acab1faf9`).
Replay stimulus: LocalStructure, normal 0.1-second updates, exterior Door at
Level 0 vertical Wall `(16,7)` opened at Reset; S1–S0 Door at `(13,7)` opened
at 1 simulated second. Cell coordinates are zero-based from the top-left.
At 1.1 seconds:

| Cell | Pressure Pa | Wind (x,y) m/s | Drag (x,y) N | Pressure load (x,y) N | Resultant (x,y) N |
|---|---:|---:|---:|---:|---:|
| (13,9) | 27,658.74 | (2.20,1.79) | (1.04,0.85) | (-52.35,-33.63) | (-51.30,-32.78) |
| (13,8) | 27,524.21 | (7.57,7.05) | (13.18,12.28) | (-87.63,406.52) | (-74.45,418.80) |

Bottom-row pressures `(13,9)` / `(14,9)` / `(15,9)` were
27,658.74 / 27,868.13 / 28,028.85 Pa. A weaker adverse gradient was already present
in this particular pre-opening sample at 1 second (21,714.81 / 21,733.34 /
21,795.34 Pa); the second Door amplifies it. This does not assert the user's
unspecified opening delay was exactly 1 second.

Controls already performed during the investigation:

- Adiabatic gas transport preserves the adverse pattern: the new local solids,
  cavities, thermal pressure contacts and GUI are not required to trigger it.
- Two tiny 2×3-Cell rooms with aligned restricted inlet/outlet reproduce the
  bottom-row signs. The 16×16 Installation and its other Sectors are unnecessary.
- Making the whole right-hand boundary an unobstructed exit removes the adverse
  bottom-row gradient. This establishes boundary dependence, **not physical
  correctness** of either solution: it changes the boundary-value problem.
- The renderer uses `forceN` without flipping axes. Those arrows are the one-way
  stationary reference-object load (`drag - volume × grad(p)`), not gas velocity.
  Reproducing that arithmetic is not independent validation of the force model.

The small two-Door case is retained in `station-cell-fluid-correctness`; the
fixed-geometry study below retains a reproducible pressure/load diagnostic.
There is deliberately no regression assertion that all arrows must point toward
the exit, nor that a particular adverse-gradient sign must be preserved forever.

## Implementation review and independent checks

### Euler/HLLE flux and Wall accounting

`CellFluidCorrectness.cpp` constructs an independent textbook HLLE oracle using
conservative fluxes, explicit mirrored ghost inventories at Walls and direct
flux divergence. It does not use the solver's precomputed HLLE weights, optimized
Wall-pressure expression, pressure-cancellation gauge or activity reference.
A single CFL-unconstrained small forward-Euler step compares:

- all seven conserved quantities in each Cell;
- integrated open-face species/momentum/energy transfers;
- fixed-Wall reaction impulse;
- both axes, mixed species, unequal pressures/velocities, partial openings,
  discharge coefficients and exterior vacuum.

The optimized implementation agrees within floating-point roundoff. This checks
the implemented first-order flux, **not** whether HLLE at 2 m resolves the flow
or whether its aperture law matches a calibrated nozzle model. The sampled Wall
oracle states are subsonic; this is not an exhaustive proof of strong-expansion
Wall clipping, entropy stability, every admissible state or every topology.

The pressure gauge is local: each Cell's own pressure cancels over its complete
geometrically closed boundary. It is not a remote reference-pressure source.
The reflecting pressure expression agrees with the mirrored-state HLLE flux
for the sampled conditions. Fixed Walls transfer momentum but no mass/energy.
Open-face fluxes book equal/opposite endpoint transfers.

### Two-Door invariants

A 30-update, gas-only 2×3 + 2×3 case checks:

- species and total gas energy against exterior transfers;
- total momentum against exterior pressure/momentum stress and Wall impulse;
- reflection in X, interchange of X/Y axes, and reversal of interior face
  endpoint/normal orientation;
- equality with every Cell forced active by reapplying unchanged opening
  controls, exercising the full face calculation instead of inactive-face skips;
- reflected aerodynamic-field results with unchanged scalar pressure/density.

These tests pass. Existing allocation-free success, acoustic propagation,
positivity/refusal, shock/reflection, phase/source and prolonged Door-blowdown
checks also remain green. Passing conservation does **not** establish spatial
accuracy: a conservative numerical viscosity can still dominate the answer.

### Restricted-opening boundary policy

The current kernel uses `openArea = faceArea × openFraction × Cd` and a reflecting
complement `faceArea - openArea`. Thus the reduced effective area affects momentum
as well as mass/energy; the remainder acts as reflecting boundary in this model.
This is the documented prototype aperture treatment in
[ADR-0004](adr/0004-cell-atmosphere-and-horizontal-momentum.md), not a resolved
0.9×2 m jet or an independently calibrated analytic Door nozzle. This audit does
not silently replace that policy with another discharge/pressure model.

## Numerical diffusion diagnostic in the actual S0 replay

At 1.1 seconds, evaluate the implemented HLLE operator at the committed gas
state. For X-momentum entering `(13,9)` from `(13,8)`, split the flux into
weighted physical advective terms and `sL*sR/(sR-sL) × (U_R-U_L)`:

| Instantaneous momentum-rate contribution | N |
|---|---:|
| Physical advective X-momentum from the Cell above | +58.35 |
| HLLE numerical-diffusion X-momentum from the Cell above | **+1,857.87** |
| Physical advective contribution across the right face | -12.91 |
| Numerical-diffusion contribution across the right face | +218.64 |

These are gas momentum rates on Cell faces, **not forces measured on the reference
object**, and not averages over the preceding host update. The vertical face has
no physical pressure contribution in its tangential X-momentum component. The
large positive tangential diffusion is artificial momentum mixing, not a resolved
physical shear stress or turbulence model. The code intentionally uses HLLE
mixing, but its dominance here undermines a purely physical explanation of the
coarse corner pressure pattern. The associated pressure and Wall response can
balance strongly diffused momentum rather than a resolved jet.

## Fixed-geometry resolution and temporal study

`station-cell-fluid-resolution-study` constructs native meshes directly:

- Two adjoining 6 m × 8 m rooms, 3 m storage height, initially pure N2 at
  101325 Pa and 293.15 K, zero velocity, no sources or thermal exchange.
- Aligned 0.9 m × 2 m openings centred at y=3 m; Cd=0.7. The right exterior
  opening is active from t=0, the partition opening from t=1 second.
- Total aperture area is held at 1.8 m² at each resolution using overlap with
  the same physical opening. Total volume and room dimensions are unchanged.
- Sample the same three physical 2 m × 2 m bottom-row patches in the downstream
  room. Pressure is volume-averaged; velocity is mass-weighted. Force is the
  average **diagnostic reference-load estimate** at the fine Cell centres, not
  resolved flow around a finite object or a physical force measurement.

Refinement below 2 m is **research-only**, not a new production geometry option,
a default change or a recommended fidelity fallback. Results from this pure-N2
minimal geometry are not presented as the original Installation's exact values.
The [63-row raw CSV](benchmarks/cell-fluid-correctness-audit.csv) includes samples
at 1, 1.1 and 2 seconds. Numerical source: `eeb8c3f` plus audit-only tests/tooling;
`CellFluid.cpp` is unchanged. Release `-O3 -DNDEBUG`, serial development host as
in the [integrated rollout report](integrated-thermal-rollout.md).

At t=2 seconds, host updates of 0.1 seconds:

| Mesh spacing m | Gas Cells | East-minus-west bottom-patch pressure Pa | Middle-patch mean estimated Fx N |
|---:|---:|---:|---:|
| 2 | 24 | 187.254 | -46.433 |
| 1 | 96 | 116.682 | -28.658 |
| 0.5 | 384 | 58.590 | -15.153 |
| 0.25 | 1,536 | 31.752 | -8.323 |
| 0.125 | 6,144 | 18.187 | -4.833 |

The coarse-to-finest adverse pressure difference changes by over 10×; the load
proxy changes by about 90%. The finest two results still differ materially, so
**no converged physical answer, exact sign or error bound is claimed**. Mean
chamber pressure also changes with resolution. This study is evidence against
trusting the current coarse prediction, not an independent high-fidelity truth
solution. Further flux/mesh/reference validation is required.

By contrast, reducing the host step at fixed h=2 m barely changes that sample:

| Host step s | East-minus-west pressure Pa | Middle-patch estimated Fx N |
|---:|---:|---:|
| 0.1 | 187.254 | -46.433 |
| 0.01 | 187.263 | -46.435 |
| 0.001 | 187.353 | -46.457 |

The solver already takes acoustic CFL substeps; source/GUI cadence is not the
primary cause. Across recorded samples, maximum absolute conservation residuals
were 4.92e-14 kg N2, 1.66e-8 J energy, 7.14e-9 kg m/s X momentum and
2.25e-9 kg m/s Y momentum. These small bookkeeping residuals coexist with large
spatial sensitivity.

## Reproduce / validation

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --config Release -j 16
ctest --test-dir build-linux -C Release -j 16 --output-on-failure

build-linux/station-systems/tests/station-cell-fluid-correctness
# Run diagnostic studies separately from builds/CTest; one process per sample.
for h in 2 1 0.5 0.25 0.125; do
  build-linux/station-systems/tests/station-cell-fluid-resolution-study "$h" 0.1
done
for dt in 0.01 0.001; do
  build-linux/station-systems/tests/station-cell-fluid-resolution-study 2 "$dt"
done
```

Full incremental Release build succeeded. **142/142 CTest tests passed**, `-j 16`,
164.91 seconds, including the new independent flux/invariant test. Refinement
study output is recorded evidence, not a CTest assertion of physical adequacy.

## Follow-up recommendation

Treat the current Cell-scale wind/load fields as qualitative pending validation.
Do not describe these corner arrows as verified physical stagnation/object motion.
Evaluate a less diffusive/contact-resolving, positivity-preserving flux and the
restricted-aperture/Wall treatment against independent compressible reference
cases and fixed-geometry two-Door convergence tests. Preserve species/energy/
momentum accounting, whole-call refusal/rollback, acoustic physics and successful
numerical no-allocation guarantees, and remeasure serial performance. Any method,
accuracy or budget tradeoff needs explicit review; a blind viscosity coefficient
change, refined production mesh, artificial attraction to the breach or sign-clamp
is not a justified bug fix.
