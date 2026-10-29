# Native structural thermal calibration — candidate C1 (#24)

**Provisional engineering study, not reviewed production defaults.** This is the
physical-parameter gate under [#22](https://github.com/ajare/numina-space-industries/issues/22)
and [ADR-0007](adr/0007-native-structural-thermal-contract.md). It does not change
native playback, standalone compartments, core presets, YAML or backend selection.
The opt-in [versioned candidate API](native-thermal-api.md) was added in #26
after the #25 cost gate; rollout still requires #35. C1 is a study identifier,
not a save-schema/model revision. The API revision additionally records the
reconciliation with runtime species constants; the frozen study below remains
reproducible and the executable also checks the installed candidate's anchors.

## Reproduce

From a Release build configured with `BUILD_TESTING=ON`:

```sh
cmake --build build-linux --config Release --target nsi-thermal-calibration -j 16
build-linux/systems-bridge/nsi-thermal-calibration
ctest --test-dir build-linux -C Release -j 16 --output-on-failure
```

`systems-bridge/tests/ThermalCalibration.cpp` is an executable fixture, deliberately
not itself an installed material or gas-contact API. Its frozen positive resistances are
checked by independently solving steady-state balances, not re-fitted in the test.
It also detects changes to the existing core Hull/Partition anchors and bridge
exterior capacity. Roundoff-level agreement is obtained; acceptance remains the
parent's **5%**, not a claim of equivalent physical accuracy.

## Reference boundaries and passive resistance split

All fixtures use **1 m²**, zero solar gain, pressure **101325 Pa**, dry **79% N2,
21% O2 by mole**, gas-film continuum coefficient **20 W/(m² K)** and accommodation
**0.8**. The film coefficient at these conditions is 19.9950194961 W/(m² K).
Interior surface emissivity is **0.8**. Each reference side faces a large black
isothermal cavity at that side's gas temperature, so irradiation is `sigma Tg^4`.
These are prescribed calibration reservoirs, not a claim that every dynamic
Sector's cavity equals its gas temperature. Interior radiation and gas contact
act **in parallel**, not as two series films. In actual playback the algebraic
cavity of #30 determines irradiation independently of gas.

For gas/cavity temperature `Tg` and surface temperature `Ts`, flux into a side is:

`F(Tg,Ts) = h (Tg-Ts) + 0.8 sigma (Tg^4-Ts^4)`.

Hull fixtures hold gas/cavity at **293.15 K (20 °C)** and exterior space at **3 K**.
For target flux `q`, exterior temperature `To` and interior temperature `Ti`:

```
To = (q/(epsilon_out sigma) + 3^4)^(1/4)
F(293.15, Ti) = q
R_solid = (Ti-To)/q
```

This recovers ADR-0002's rounded flux anchors without interpreting its effective
U-values as bare solid conductance. For example, BareSkin has Ti=283.952927 K,
To=265.085868 K, so its positive solid resistance is 0.0842279432 m² K/W.
The gas-film, cavity exchange, solid and exterior radiation all remain passive.
Exterior radiation is nonlinear, **not** another constant conductive resistance
to vacuum. No target/spec amendment is required.

Partition fixtures hold the hot gas/cavity at **303.15 K**, cold at **283.15 K**
(mean 293.15 K), with `q = U_target × 20 K`. Solve
`F(303.15,Th)=q`, `F(283.15,Tc)=-q`, then `R_solid=(Th-Tc)/q`.
The film's kinetic reference temperature is fixed at the mean 293.15 K for both
fixtures. A future dynamic contact should use a positive symmetric mean of its
gas/surface temperatures; the steady calibration is explicitly at this reference.

| Assembly | R solid (m² K/W) | Exterior epsilon | Absorptivity | Solved flux/U |
|---|---:|---:|---:|---:|
| BareSkin | 0.0842279432024 | 0.80 | 0.40 | 224 W/m² |
| StandardInsulated | 1.63286240745 | 0.85 | 0.20 | 62 W/m² |
| MultiLayerBlanket | 19.8930878347 | 0.03 | 0.15 | 3.8 W/m² |
| BarePartition | 0.118607760627 | — | 0.40 | 5 W/(m² K) |
| ThermalBreak | 0.918610103986 | — | 0.40 | 1 W/(m² K) |
| InsulatedPartition | 3.25194391034 | — | 0.40 | 0.3 W/(m² K) |

All interior/partition faces have epsilon=0.8; absorptivity on internal faces is
inactive without solar exposure. Hull exterior optics are retained from ADR-0002;
partition absorptivity is an explicit provisional assumption. Sheet/junction
paths are absent from the isolated 1-D anchor fixtures (no lateral temperature
gradient). They must not create extra through-panel paths that bypass R solid.

## Capacity, sheets, seams and Door budgets

| Physical assembly | Total areal C (J/(m² K)) | Side allocation | Total sheet k*d (W/K per square) | Seam per length (W/(m K)) |
|---|---:|---:|---:|---:|
| Hull panel, exterior Door leaf/frame | 400 | 50% / 50% | 0.02 | 0.1 |
| Partition/shared floor, internal Door leaf/frame | 2000 | 50% / 50% | 0.10 | 0.1 |

The exterior **400 J/(m² K)** exactly preserves `0.4 kJ/(m² K)` from
`expandThermal` in `systems-bridge/src/Atmosphere.cpp`. This small capacity
is a compatibility anchor, not a validated spacecraft panel mass. Each side gets
half, never the full total. Internal 2000 is a new assumption equivalent to
2 kg/m² at 1000 J/(kg K); it stores heat that legacy massless partitions cannot.
Neither capacity contributes to gas EOS, pressure or sound speed.

Sheet values are effective lightweight-skin assumptions, roughly consistent with
volumetric capacity 2.4 MJ/(m³ K), conductivity 120 W/(m K) and the stated areal
capacities. They are not insulation conductivities. For a panel of width `w` and
centre spacing `l`, connect actual same-side neighbours with
`G_sheet = (k*d / 2) w/l` (half sheet allocation per side). A seam of length `L`
has **total** `G_seam = 0.1 L W/K`, also split half per side. Where both sheet and
seam resistances lie along one route, put them in series, not duplicate parallel
links. Never connect merely nearby panels or make an isothermal bus. These
junction values are deliberately finite assumptions, not measurements of a real
fastener. Later geometry work must state which physical contacts exist.

Door frame/leaf capacities use their **parent assembly's** areal values; there
is no extra anonymous frame mass. For original panel area A and clear leaf area
1.8 m², allocate `(A-1.8) C_areal` to fixed material/frame and `1.8 C_areal` to
the leaf, then split each between sides. The total remains `A C_areal`. Openings
relocate the leaf energy, not delete/reinitialize it; do not count an extra frame
layer over the full panel. Tall above-aperture material remains in the fixed
budget. Exterior frames therefore do not break the exterior capacity anchor.

Intended transient effect: equal side capacities give an isolated through-panel
difference-decay timescale `R_solid C_areal / 4` (area cancels). Hull times are
about **8.42, 163, 1989 s**; partition times **59.3, 459, 1626 s**. Film/cavity,
lateral exchange and radiation change actual cooldown times. The initial 293.15 K
solid energy is `C T`, not an added heating impulse. Persistent partition heat and
local finite propagation are intentional; these numbers are not whole-room
cooldown predictions.

## Kinetic pressure bridge: derivation, not a guessed transition

Use a small-temperature-difference ideal-gas free-molecular model. Species i has
molar mass `M_i`, molar constant-volume heat capacity `cv_i`, and partial pressure
`p_i = x_i p`. Its incident molar flux at reference T is
`p_i / sqrt(2 pi M_i R T)`. The temperature derivative of energy carried by an
effusing molecule is `cv_i + R/2` (translational flux weighting adds R/2 to the
bulk molar energy derivative). With an effective accommodation `a_eff`:

```
K(T,x,alpha) = sum_i x_i a_eff (cv_i + R/2) / sqrt(2 pi M_i R T)
a_eff = alpha/(2-alpha)
h_fm = K p
h = h_fm / (1 + h_fm/h_cont)
```

C1 chooses the **symmetric two-surface effective accommodation** convention,
alpha=0.8 -> a_eff=2/3. This is a provisional bulk-gas-to-surface closure, not a
measured single-wall accommodation. A direct one-wall convention would use alpha
instead; changing that convention must trigger a parameter revision/review.
The mixture sum is over **wet mole fractions**, not mass fractions or an average
molecular mass. Species constants used in the reproducible study:

| Species | M (kg/mol) | cv (J/(mol K)) |
|---|---:|---:|
| O2 | 0.0319988 | 20.8 |
| N2 | 0.0280134 | 20.8 |
| CO2 | 0.04401 | 28.8 |
| H2O vapour | 0.01801528 | 25.3 |

`R=8.314462618 J/(mol K)`, `sigma=5.670374419e-8 W/(m² K⁴)`.
These rounded constant-cv ideal-gas inputs belong to this study; the production
implementation should reconcile them with its species thermodynamics rather than
quietly duplicate a second EOS table.

For reference air, **K=0.792431864619 W/(m² K Pa)**. Half-continuum contact occurs
at **h_cont/K = 25.2387629687 Pa**. This pressure is a **derived result**, not a
transition-pressure input. The bridge has `h(0)=0`, `0<=h<h_cont` and, at fixed T,
mixture and accommodation, `dh/dp = h_cont² K/(h_cont+Kp)² >= 0`. Thus reducing
pressure cannot increase absolute film flux at a fixed temperature difference.
The rational form handles p=0 and alpha=0 without divisions by zero; film changes
never rescale solid R, capacity, sheet/junction paths or radiation.

The test covers **84,140 samples**: seven mixtures (dry air, four pure species,
two wet/CO2-rich mixtures), five temperatures (100, 200, 293.15, 600, 1000 K), four
accommodations (0, 0.1, 0.8, 1), and 601 log-pressure points from 1e-16 to 1e8 Pa,
plus exact vacuum. Checks include finiteness, positivity, bounds, monotonic signed
flux magnitudes, local continuity/analytic derivative, molecular asymptote and
derived crossover. The wide range is a **mathematical robustness sweep**, not a
claim water stays vapour at 100 K or constant-cv accuracy at 1000 K. Runtime phase
physics and unresolved-gas detachment remain separate contracts. Decreasing
pressure does not imply monotonically decreasing gas temperature.

## Provenance, uncertainty and review

- Flux/U anchors and Hull optics: [ADR-0002](adr/0002-geometry-authored-thermal-coupling.md).
  The explicit 20 °C/3 K Hull reservoirs reproduce its rounded calibration values;
  its legacy U-values are not reused as bare solid conductances.
- Free-molecular relation: [AIAA-82-0888](https://www.mhtlab.uwaterloo.ca/pdf_papers/mhtl82-2.pdf)
  discusses effective accommodation; [DSPE free-molecular heat transfer](https://www.dspe.nl/knowledge/thermomechanics/chapter-2-in-depth/conduction-in-gasses/regime-1-free-molecular-heat-transfer/)
  gives `lambda=(gamma+1)/(2(gamma-1))*sqrt(R/(2 pi M T))`.
  With `gamma=1+R/cv` this is the K factor above before accommodation.
  [High-Knudsen-number measurements](https://pmc.ncbi.nlm.nih.gov/articles/PMC7143282/)
  give the equivalent `q=(alpha/8)*((gamma+1)/(gamma-1))*(v_mean/T)*p*dT`
  and illustrate dependence on actual surface treatment. These support the
  pressure-linear asymptote, not the chosen C1 parameters as validated products.
- h_cont=20, alpha=0.8, epsilon_inside=0.8, partition capacity, sheet/seam values
  and symmetric accommodation convention are **engineering assumptions**.
  Film and cavity together limit reference transfer; the legacy ~10 W/(m² K)
  assembly observation is not a requirement that a single film equal 10.
- Accommodation/surface contamination, real multilayer blankets, fastener bridges,
  temperature-dependent cv/conductivity, convection and detailed view factors are
  unvalidated. The resistance bridge is a smooth interpolation, not a full
  rarefied-gas solution. Neither extreme pressure nor arbitrary temperature
  differences justify extrapolating it as experimentally accurate.
- Calibration is nonunique: different continuum films, cavity boundaries or
  optical properties change the positive R split. Exact anchor fitting does not
  resolve transient uncertainty. Review should compare alternate films (10–30),
  alpha (0.1–1), capacities and seams in the cost/solver prototypes before adopting
  defaults. No production parameter or performance approval is implied here.
