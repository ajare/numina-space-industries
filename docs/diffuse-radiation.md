# Visibility-free diffuse Sector radiation (#30)

The opt-in `LocalStructure` backend now uses `station::DiffuseRadiation`
(`<station/DiffuseRadiation.h>`) under ADR-0007 and #22. The model revision is
`local-structure-v2-cavity-candidate`; material revision remains
`C1-v1-provisional`. The default backend, standalone compartments and YAML are
unchanged. Active Door thermal poses/portals remain #31; rollout remains #35.

## Physical approximation

There is one algebraic irradiance H (W/m²) per **geometric Interior Sector**,
including Interior Void atmosphere presets. Tall Cell claims share their source
Sector's field. Fields have no energy, capacity or temperature, and operate at
all pressures, not just after evacuation. For w_i = epsilon_i A_i:

```
sum_i w_i (sigma T_i^4 - H_s) + sum_portals A (H_neighbour - H_s) = 0
surface power into structure = w_i (H_s - sigma T_i^4)
portal power from a to b = A (H_a - H_b)
```

Each reciprocal internal portal is stored/evaluated once; the other endpoint
receives its opposite power. Exterior fields are sigma T_space^4, using the
existing sink temperature. Same-Sector portals are inert. Missing gas does not
create an exterior reservoir. Exterior solid faces retain their separate local
emission and projected solar inputs; interior faces are not direct deep-space
radiators.

This is deliberately **visibility-free, well-mixed diffuse radiation**. It
ignores intra-Sector occlusion, directional illumination and spatial view
factors. Reflecting unmodelled adiabatic faces have zero emissive/absorbing
weight, but remain geometrically opaque: a missing Partition material creates
neither capacity nor a portal through a present Wall/floor. The approximation
does not make radiation pass through these boundaries. Detailed visibility,
view factors and directional solar remain out of scope.

For an isothermal enclosure with total absorbing area W and exterior aperture A,

```
H = (W sigma T_room^4 + A sigma T_space^4) / (W + A)
loss = A W/(A+W) sigma (T_room^4 - T_space^4)
```

Thus loss is bounded by the black-aperture limit, not full room surface area.
A closed isothermal enclosure transfers zero heat. A connected component with
neither positive absorbing area nor exterior exchange has undetermined H:
its transfer is zero and `trialIrradiance()` returns unavailable. Boundary-only
components instead have a determined field and no invented thermal mass.

## Sparse nonlinear coupling

Let M have diagonal `sum(w) + sum(portal area)` and reciprocal off-diagonal
`-A`. It is SPD on anchored components; unanchored components are excluded by a
Reset-time connected-component pass. No all-surface matrix is constructed.

At each nonlinear trial use a **common** nonnegative slope
`k = 4 sigma max(T_surface)^3` and offsets `b_i = k T_i - sigma T_i^4`.
This gives exact physical power at that trial while retaining a symmetric
positive-semidefinite frozen thermal operator:

```
H = M^-1 [sum_i w_i (k T_i - b_i) + exterior area * H_space]
L x = k [w_i x_i - w_i (M^-1 sum_j w_j x_j)_s]
source_i = w_i [b_i + (M^-1 (boundary - sum_j w_j b_j))_s]
```

Weights are accumulated per owning cavity; multiple surfaces may share a body.
The diagonal preconditioner uses the conservative bound `k sum(w_i)`.
Affine sources may be signed because they include nonlinear offsets; this is
not negative physical emissivity or an active boundary. The outer solver checks
finite nonnegative Kelvin and the original nonlinear energy equations, refining
or refusing rather than clipping temperature/heat.

M actions and solves use preallocated long-double vectors, sparse portal edges
and Jacobi-PCG (at most 256 iterations per solve). Field residual tolerance is
`1e-15 + 1e-19 sum(abs(rhs))`. Every field solve and inner iteration charges the
**same whole-call counter** as the outer thermal solver, including freezes,
linear actions, final nonlinear residuals and boundary accounting. The work
counter is attached only for the duration of `solve()`. Poor conditioning or
exhaustion refuses; it does not drop portals or merge fields. Storage is linear
in nodes, surfaces, cavities and portals; successful updates allocate nothing.
The structural preflight's per-claim storage allowance also covers the new
owner-to-cavity mapping and its construction scratch.

The exterior ledger independently sums exterior portal powers from the physical
trial fields (once per portal), rather than treating the sum of node heat as
its own audit. Internal surface/cavity/portal exchange has no stored-energy
account and cancels. Existing energy tolerance, <=0.1 s intervals, whole-update
rollback, retained refused time and last committed observations remain.
`trialIrradiance()`/`trialPortalPowerW()` are numerical **trial diagnostics**, not
published playback history; #32 owns committed cavity/path observations.

## Geometry wiring and staging

Reset constructs the owner map independently of gas inventory, adds each
interior structural face once, and converts non-Door opening metadata into
reciprocal or exterior portals. Breaches use their full physical height coverage
without gas Cd or duplicate radiating panels. No-Hull still opts out completely.
Door controls currently affect gas only: Reset-time leaf ownership remains and
no Door radiation portal is activated until #31 implements energy-preserving
leaf relocation and coefficient updates. This is not the completed parent model.

## Evidence

`station-native-thermal-consumer` checks analytic two-cavity fields and reciprocal
power, a 12-cavity series-resistance chain, sparse frozen symmetry,
closed isothermal exchange, unavailable connected
reflecting components, boundary-only fields, the analytic grey aperture and
black-aperture bound, analytic grey-enclosure time refinement, implicit
conservation, nested-budget refusal/publication
retention, and zero successful-step allocations. Existing gas pressure/film
coverage remains in the same executable; cavity physics has no pressure gate.

`nsi-structural-playback` adds isolated closed-room redistribution, opaque
unmodelled partitions, static breach aperture bounds, tall owner/cavity counts
and Interior Void cavity existence. Its existing 1x/10x early/late/cross-slice
rollback, retry, gas/solid audit and allocation checks now exercise real cavities.

Incremental Release build succeeded; all **136** CTest tests passed with
`ctest --test-dir build-linux -C Release -j 16 --output-on-failure`
(162.67 s). This is component/integration validation, not the serial #35
performance or physical-default approval.
