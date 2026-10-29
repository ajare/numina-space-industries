# Opt-in sparse nonlinear thermal numerical layer (#28)

`station::NativeThermalSolver` (`<station/NativeThermal.h>`) extends the
[finite-capacity body/contact boundary](native-thermal-api.md). It does **not**
select a playback backend, replace geometry paths yet, alter standalone
compartments or enable provisional material defaults. The #30
[diffuse radiation extension](diffuse-radiation.md) now supplies real cavities.
[ADR-0007](adr/0007-native-structural-thermal-contract.md) and #22 remain the
contract. Integration/whole-playback rollback is #29, actual cavity physics is
#30, and performance/default rollout remains #35. Public namespaces/targets,
YAML and `C1-v1-provisional` presets remain unchanged; the model revision is now
`local-structure-v3-door-candidate`.

## Inputs, units and publication

Construct once with fixed body/gas counts and solid, gas and radiation contacts.
Contact validation uses the #26 boundary; node/contact count additions are
checked before sizing their workspaces. Vector allocation/length failures throw
at construction, not a successful step. Storage is O(nodes + contacts), including
iteration and whole-call staging buffers; there is no star-network assumption,
dense graph matrix or zero-capacity cavity node.

Each solve takes immutable `ThermalBody` inventory and `NativeThermalGas` values.
A resolved gas supplies current **gas-only** cv in J/K, internal energy in J,
wet species mole fractions and `pressurePaPerK = n R / volume`. Its species,
volume and cv are fixed during that source call; pressure is recomputed from
its trial temperature. The gas owner must derive these consistent quantities
from its conserved species inventory, not include structural capacity. Kinetic
energy, advection, phase changes and sources outside the supplied radiation
boundary remain the caller's responsibility. Integrate these through #29's
thermal/source splitting, separately from acoustic steps; never reapply heat
already booked here.

Unresolved gas is detached without changing its retained input energy. Zero
pressure also gives exactly zero film even for a resolved gas. Solid conduction,
capacity and radiation do not disappear. Invalid/nonfinite inventories,
fractions, time or boundary values refuse; resolved gas requires positive
capacity and energy. Solid Kelvin bounds follow `ThermalBody` (including 0 K).
No temperature or energy clamp is used.

For every **Exterior** radiation contact, pass one `NativeThermalExterior`, in
contact order. `spaceTemperatureK` specifies its radiative field;
`absorbedSolarW` is **already projected and absorbed power** from the existing
solar-profile/area approximation. The solver must not multiply it by the full
surface area or absorptivity a second time. Cavity contacts require an explicit
algebraic operator; they are never silently treated as exterior or omitted.

On success, `heatJ()` and `temperatureK()` publish body nodes followed by gas
nodes. Apply heat exactly once with opposite film/conduction bookings already
included. Inputs are never mutated. Whole-call failure preserves **both** last
successful output arrays, even after internally accepted intervals; no refused
time has been consumed by the caller. `diagnostic()` is a static string view;
`report()` describes the attempted call, not the last committed call. Solver
instances/operators are not reentrant or thread-safe.

## Passive implicit iteration and refusal

Each interval solves backward Euler:

`C (Tnew - Told) = dt [source(Tnew) - L(Tnew) Tnew]`.

Intervals are at most 0.1 simulated seconds (the caller may request smaller).
Picard iteration freezes nonnegative local film conductances at the current
trial mean temperature and pressure/current mixture. Exterior radiation uses
the **exact nonnegative secant**

`g = epsilon sigma A (T + Tspace) (T² + Tspace²)`;
`power = g (Tspace - T) + absorbedSolarW`.

This is local `epsilon sigma A (T^4 - Tspace^4)` radiation, not a conductive
vacuum contact. The frozen linear operator is sparse symmetric passive, solved
in temperature increments using Jacobi-preconditioned conjugate gradients in
long-double workspaces. Detached gas rows have no flux; no invented capacity
or singular zero-capacity radiation field is inserted.

The algebraic extension's frozen operator must also be passive and symmetric.
`freeze()` fills its diagonal and affine source at the supplied trial, and
`apply()` computes Lx without materialising a surface matrix. Implementations
own any sparse cavity fields/elimination workspace and may use matrix-free
low-rank operations. They must allocate nothing, return finite values, leave
detached gas untouched, and have no committed state changes. `freeze()` returning
false refuses that attempt. The callback contract is trusted, like contact
ownership: this layer does not prove passivity of arbitrary external code.
The original pair fixture remains; real cavity fixtures and their sparse
operator/independent portal audit are documented in [diffuse radiation](diffuse-radiation.md).
Affine sources may be signed to express exact nonlinear offsets. Operators can
attach to the shared work budget, report failed matrix actions through
`healthy()`, and supply an independent `boundaryPowerW()` audit.

After a linear solve, coefficients are recomputed at the new trial and the
nonlinear node energy-equation residual is checked separately from CG's flux
residual. Accepted nonlinear tolerance is `1e-8 J + 1e-13 sum(abs(node heat))`,
inside the required `1e-7 J + 1e-12 sum(abs(applied node heat))` envelope.
Linear iteration uses `1e-11 J + 1e-18 sum(abs(rhs))`; a stringent absolute
floor and long-double increments avoid subtracting large inventories to
resolve tiny heat. Every accepted heat is evaluated from the final flux:
solid/film edges are equal/opposite; boundary exchange is accumulated separately
in long double. Published heat is checked against the independent boundary
ledger at the required energy residual scale. Temperatures are derived from
the booked interval heat, not clamped or silently re-equilibrated.

`NativeThermalReport` separates maximum accepted nonlinear/linear flux residuals,
last nonlinear trial residual (also available on convergence refusal), final
whole-call energy residual, signed exterior heat (positive into nodes), interval,
refinement, nonlinear/linear iteration and whole-call work counts. For a supplied
algebraic operator the exterior ledger includes its net boundary power; internal
reciprocal transfers must cancel in that operator.

Nonlinear/linear exhaustion or invalid trial state retries from the same interval
start with half its duration. A successful smaller interval advances only that
duration; **all remaining requested time is still integrated**. Refinement depth
is bounded per attempted interval; coefficient freezes and CG iterations share
one deterministic whole-call work budget across all intervals/refinements. Each
freeze costs one work unit (including fixed operator evaluations), each CG
iteration costs one. Callback implementations must themselves bound their
algebraic solve work; `DiffuseRadiation` charges all nested field solves and
iterations to this same whole-call counter. Default limits are 32 nonlinear iterations, 256 CG iterations, 12
refinements, 100,000 whole-call units. Exhaustion, representational time failure,
invalid Kelvin or energy residual failure refuses the whole call. There is no
wall-clock criterion, dropped heat/time or automatic fidelity reduction.

## Component evidence

`station-native-thermal-consumer` uses only the installed public target. Fixtures
cover gas + two-body capacity-weighted equilibrium and conservation, persistent
local gradients/disconnected patches, a non-star cycle, same-temperature body
addition, a stiff two-body exact backward-Euler result, current-mixture and
log-pressure film sweeps, zero-pressure/unresolved detachment with continued
solid conduction/radiation, projected solar counted once, early/late/nonlinear
budget rollback and invalid inventory/time. A matrix-free algebraic pair fixture
checks conservative extension without an irradiance energy node. Allocation
tracking wraps successful linear/algebraic **and** nonlinear gas/radiation calls
after construction.

An analytic one-body `dT/dt = -sigma A T^4/C` fixture demonstrates time refinement:
for C=100 J/K, T0=1000 K, A=1 m², epsilon=1, Tspace=0 K over 0.1 s, errors fall
from about **4.20959 K** at dt=0.1 to **1.16144 K** at dt=0.025. Restricting the
nonlinear iteration limit forces bounded refinement and still closes the energy
audit. These are numerical component tests, not an integrated accuracy,
performance or default-rollout approval.

Validation: full incremental **Release** build succeeded; all **135** CTest tests
passed with `ctest --test-dir build-linux -C Release -j 16 --output-on-failure`
(162.54 s on the development machine). Whitespace checks passed. No integrated
#35 performance claim is made.
