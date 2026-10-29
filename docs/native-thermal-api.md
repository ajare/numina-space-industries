# Native thermal preset and body/contact boundary (#26)

This is an **opt-in candidate API**, not an enabled playback backend or approval
of physical defaults. [ADR-0007](adr/0007-native-structural-thermal-contract.md),
[#22](https://github.com/ajare/numina-space-industries/issues/22),
[C1 calibration](thermal-calibration.md) and the [P1 cost review](structural-cost-review.md)
remain authoritative. Geometry expansion (#27) and the opt-in
[variable/nonlinear solver (#28)](native-thermal-solver.md) are now available as
separate components. Playback integration (#29), cavities/portals and rollout
(#35) remain later work. No concurrent second heat model, object inventory,
placement, anonymous Sector mass or parallelisation is introduced here.

## Core intensive descriptions

`<nsi/core/ThermalPresets.h>` (also in `Core.h`) exposes
`nativeThermalSpec(Installation::HullType/PartitionType)` and validation of
`ThermalAssemblySpec` / `ThermalGasFilmSpec`. Legacy `hullSpec` and `partitionSpec`
remain unchanged; their effective U-values must **not** become bare solid
conductances with another film/radiation resistance layered on top.

The candidate descriptions carry the C1 frozen positive solid resistances,
total areal capacities (Hull 400, Partition 2000 J/(m² K)), 50/50 side allocation,
per-side optical coefficients, total sheet and seam conductance and film settings
(20 W/(m² K), accommodation 0.8). Hull first/second sides mean inside/outside;
partitions have identical side optics. These are provisional engineering
assumptions, not manufacturer claims. The revision is
`nsi::core::NativeThermalPresetRevision = "C1-v1-provisional"`.

For physical area A, create two bodies with capacities `A C_areal f` and
`A C_areal (1-f)` and join them with `G = A/R_solid` W/K. Do not give each side
the full assembly capacity. Actual same-side physical neighbours may use
`G_sheet = (sheet/2) width/spacing`; seam total conductance is `junction × length`,
split half per side. Where sheet and seam resistances share a route, combine
in series, not duplicate parallel links. No proximity-based/isothermal bus.
Door leaf/frame use the parent assembly: split `1.8 C_areal` into the leaf,
`(A-1.8) C_areal` into fixed material, then allocate each to its sides. Opening
must preserve the leaf's energy, not construct new bodies.

Validation rejects nonfinite values, nonpositive capacity/solid resistance,
side fractions outside (0,1), negative local/film conductance and optics or
accommodation outside [0,1]. Zero local conductance, optics or film are passive
and allowed. The API names carry SI units; it cannot detect a caller supplying
numerically valid values in the wrong units. Geometry expansion must also check
area multiplication, count limits, stable IDs and unique ownership (#27).

## station-systems finite-capacity state

`<station/ThermalBody.h>` is independent of core geometry and gas configuration.
`station::NativeThermalBodySettings` carries capacity in **J/K** and an initial
**Kelvin** temperature (293.15 K by default). It deliberately does not reuse the
legacy machine-casing `station::ThermalBodySettings` in `Configuration.h`, whose
units are kJ/K and Celsius and whose sink settings/standalone behaviour are
unchanged.

`ThermalBody` stores finite positive constant capacity and nonnegative energy
with the convention `E = C T`, zero at 0 K. Construction/Reset initializes energy;
it is not an applied source. `fromCarriedEnergy(C,E)` restores inventory without
reinitializing temperature. `addHeat(Q)` validates the candidate state before
committing; invalid/overflowing values throw `invalid_argument` without changing
it. Successful body operations allocate nothing. Network conservation and
transactional rollback of multiple bodies/gas/ledgers remain solver duties.
There is no capacity setter that could silently reprice stored energy.

The separate contact descriptions are:

- `ThermalSolidContact`: two distinct body indices and finite nonnegative W/K.
- `ThermalGasContact`: body index, gas Cell index, physical m² and film settings.
- `ThermalRadiationContact`: body index, physical m², optics and explicit Cavity
  or Exterior membership. A cavity index is not a thermal body/energy node.

`validateThermalContacts` checks indices, boundary tags and passive coefficients
at Reset, using independent body/gas/cavity counts. Multiple surface contacts per
body are allowed; the geometry owner must prevent duplicate area. No registration,
portal solver or moving-body implementation is implied. Zero gas Cells are valid
when gas contacts are absent; structure/radiation need not disappear with gas.
Unresolved gas detachment must be handled by the integrating solver without
losing its retained inventory. No contact adds capacity to the gas EOS.

### Pressure-dependent film

`thermalGasFilmWPerM2K` implements C1's symmetric accommodation bridge with
absolute pressure **Pa**, a positive symmetric mean temperature **K** and four
**wet mole fractions**, ordered O2/N2/CO2/water vapour (sum 1 within 1e-12).
The caller may use `(T_gas+T_surface)/2`, computed without overflowing. Unresolved
or missing gas must be omitted, not supplied invented fractions/temperature.

`K = alpha/(2-alpha) sum x_i (cv_i + R/2)/sqrt(2 pi M_i R T)`;
`h = h_cont/(1 + h_cont/K/p)`, with exact zero for p=0, alpha=0 or h_cont=0.
Only this film coefficient varies with pressure, not solid resistance, capacity
or radiation. Pairwise applied film heat must be booked with opposite signs.

The installed implementation reconciles C1's rounded study cv/M constants with
**the existing CellFluid/compartment species table**: molar cv is derived as
`1000 cp_specific M - R`. There is no second EOS table. Consequently reference
K_air is **0.794574082634 W/(m² K Pa)** (study: 0.792431864619), a ~0.2703%
change; derived crossover is **25.1707177935 Pa**, not a transition-pressure input.
The frozen solid resistances remain unchanged. The real-API calibration fixture
obtains Hull fluxes 224.00001213 / 62.0000005699 / 3.80000000247 W/m² and partition
U 5.00000111187 / 1.00000004448 / 0.300000004003 W/(m² K), all within the 5% gate.
This reconciliation is explicitly part of the candidate revision, not approval
of its dynamic accuracy or extrapolation beyond the calibration assumptions.

## Future object energy bookkeeping (#21, not implemented)

An inserted body must bring its **actual carried energy** and capacity as inventory:
book `+E_carried` into the external inventory account and add the body once to
system energy. At the same initial temperature as surrounding matter, adding
`C_new T` changes neither existing temperatures nor the energy/capacity-weighted
equilibrium. It creates no heating impulse; do not book the same energy as heat.
A warm/cold body subsequently exchanges finite heat through explicit local
contacts instead of being instantly mixed with the whole Sector.

Removal must book `-E_current` as inventory leaving the system and unregister
contacts atomically. Movement within the Installation transfers ownership and
contacts while retaining C/E and changing no global inventory. A capacity change
requires a physical material/energy transaction, never a bare C edit. Future
placement/motion must validate contacts and preallocate topology/control/rollback
storage; these value types intentionally implement no object collection.
Keep gas, structure and object capacity/energy accounts separate. Never use their
labelled diagnostic total for gas pressure, sound speed or composition.

## Revisions, compatibility and tests

`station::NativeThermalModelRevision = "local-structure-v3-door-candidate"`
identifies the current body/contact and [diffuse cavity](diffuse-radiation.md)
contract (the original #26 revision was `local-structure-v1-candidate`). Future integrated snapshots/benchmarks must publish
selected backend plus model and preset revision separately from the save schema.
The calibration executable prints both candidate revision constants now. These
constants do **not** claim current playback uses the candidate model. Backend
selection remains Reset-only when implemented; the default is still legacy.

Hull/Partition enum values and all existing YAML identifiers/schema are unchanged.
No new per-surface fields are serialized, so no data migration is needed.
`hull: none`, absent Partition, Interior Void presets, standalone compartments
and public targets retain their existing semantics. No new state is hooked into
production playback in this ticket.

Release CTest coverage includes the new `nsi-core-thermal-presets` and
`station-thermal-body-consumer` tests, extended `nsi-thermal-calibration` real-API
anchors, and unchanged serialization/standalone/playback suites. Tests check
passivity/refusals, frozen values, enum compatibility, physical capacity budgets,
carried-energy restoration, same-temperature addition, isolated gas + two-body
conservation/equilibrium, gas EOS independence and pressure/mixture sweeps.
This API consumer uses the existing constant-link solver as a fixture only;
#28's separate `station-native-thermal-consumer` exercises the new opt-in
variable-contact/nonlinear numerical layer.

Validation for this change: full incremental Release build succeeded; all **133**
CTest tests passed with `ctest --test-dir build-linux -C Release -j 16
--output-on-failure` (167.10 s on the development machine). `git diff --check`
passed. This is component/compatibility evidence, not the integrated #35
performance/accuracy approval.
