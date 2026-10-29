# Station systems library

This independently configurable C++20 project owns typed Station configuration,
validation, and the configuration connection/port and Monitor catalogues that
validation requires, plus authoritative Station configuration persistence and prefab authoring. It has
no World, core, graphics, Willpower, or deployed runtime-asset dependency. It also
owns the complete coupled simulation, observation and command implementation,
with a pinned Lua 5.4 dependency and shared generic sandbox support. Persistence
uses the sibling World-independent `serialization` project and yaml-cpp (an
existing package or the same pinned source dependency as the main build).

The expand–migrate–contract extraction (#946, #949–#959) keeps configuration,
runtime rules and diagnostics in one implementation, not parallel client copies. The connection graph is configuration semantics, not editor layout.
The atmosphere observation value type and metric catalogue accompany validation
because they are the authoritative source of valid atmosphere Monitor metrics.

## Native aerodynamic-load observations

`station/CellAerodynamics.h` provides a preallocated `CellAerodynamicField` and
`cellAerodynamicLoad`: relative-flow drag plus `-V grad(p)` for a supplied body's
area, volume, drag coefficient and velocity. The default stationary reference
body has A=1 m², V=1 m³ and Cd=1. Loads are N; density is kg/m³ and dynamic
pressure is Pa. The field reconstructs a coarse volume-averaged pressure gradient
using open-face mean pressures, actual aperture/Cd and Cell volume; closed Walls
and independent Levels cannot create a fictitious pressure stencil.

Update the field after successful gas/opening changes before sampling. It never
mutates gas or advances time, and field update/sampling allocate no successful-step
heap memory. Invalid body parameters throw; unresolved gas, nonfinite gradients
or unrepresentable forces return `available=false`. This is one-way small-object
sampling, without resolved jets, shape/obstruction, lift, torque, added mass,
rarefied/Mach corrections or rigid-body integration. Actual moving bodies and
conservative opposite gas bookings require a separate coupling layer (ADR-0005).

Editor playback independently enables wind diagnostics and load calculations,
both by default; turning off wind observations leaves the gas solver's required
velocity/kinetic energy intact. Turning off loads skips field/force evaluation.

## Native Cell fluid kernel

`station/CellFluid.h` exposes a standalone `station::CellFluidSolver` through
`StationSystems::StationSystems`. Editor playback uses it through the native
geometry bridge and `CellPlaybackRuntime`; it does not run the compartment runtime
alongside it. [ADR-0004](../docs/adr/0004-cell-atmosphere-and-horizontal-momentum.md)
records the spatial model and its approximation boundaries.

The native finite-volume kernel stores four species masses, horizontal momentum
and total internal-plus-kinetic energy in contiguous preallocated buffers. Its
numerical loop calls no Lua and successful stepping allocates no heap memory.
The four species have the existing O2/N2/CO2/vapour order and calorically perfect
engineering constants. Kernel quantities are SI (kg, m, s, Pa, K, J), unlike the
compartment reporting API's kPa, Celsius and kJ. `makeCellFluidState` constructs
conserved state; `cellFluidPrimitive` derives local conditions. Species additions
are stationary gas at the supplied temperature, while removals carry local
momentum and internal/kinetic energy. Heat and source operations update cached
primitives immediately; heat cannot be deposited into a completely empty Cell.

A mesh must include all four geometrically closed faces of every Cell, including
solid Walls. Shared faces belong to one Level only. `SIZE_MAX` is an explicit
exterior-vacuum endpoint, not a way to represent an unmodelled Interior preset.
Opening fractions and discharge coefficients reduce the full face area; the
prototype uses aperture-reduced HLLE fluxes, not the standalone analytic Door
orifice law. Local mixing is conservative numerical diffusion, not calibrated
molecular diffusion. `condenseVapour` reuses the compartment latent-equilibrium
solve, removes the liquid's sensible/kinetic energy and momentum, and retains
explicit mass/energy bookings. The native bridge in `nsi/systems/CellAtmosphere.h`
now supplies authored topology, localized thermal-contact metadata, per-Cell
loads, humidity/condensation, aggregate Sector readouts and transactional source
splitting. Its optional Geometry thermal mode now solves the shared Hull and
partition contacts. Editor maps and hover readouts consume per-Cell observations;
Sector panels consume independently aggregated temperature/pressure/inventory.

Adaptive acoustic CFL stepping checks species/internal-energy positivity and
retries rejected substeps at shorter durations. A deterministic substep limit
returns `false`, a diagnostic, and the original inventory; it never commits a
partial duration. Face fluxes and fixed-Wall reaction impulse support conservation
checks (momentum flux includes pressure stress). Resting regions bypass acoustic
work within roundoff-scale pressure/composition tolerances. The constant pressure
reference and species-density background rebase once per advance to a
median-pressure representative, so uniform sources do not invalidate all resting
Cells and a localized source is not mistaken for the whole background. This is
a pressure-gauge/activity optimization, not a sound-speed or time adjustment.
Trace Cells with both
mass at most 1e-18 kg and energy at most 1e-10 J retain their conserved quantities
but have unresolved transport primitives; `resolvedGas == false` means their zero
temperature/velocity placeholders must not be displayed as physical readings.

Build the Release-only benchmark with:

```sh
cmake --build build-linux --config Release --target station-cell-fluid-benchmark -j 16
build-linux/station-systems/tests/station-cell-fluid-benchmark
```

Representative development-machine results (Ryzen AI MAX+ 395, one thread,
4,096 Cells/four gases, two warmups and ten measured fresh-start trials, mesh
construction excluded): mean **0.11 ms** at rest, **5.1 ms** for a pressure pulse,
**6.7 ms** for four full-face breaches and **7.2 ms** with every Cell disturbed,
per 0.1 simulated seconds. After one simulated second of preconditioning, the
sustained-breach case takes **9.3 ms** per 0.1 seconds (observed maximum 9.4 ms).
The fresh breach scenario takes approximately **15.7 ms** for 0.2 simulated
seconds and **88.5 ms** for one simulated second (a 10x playback update). These
are kernel measurements, not integrated playback guarantees or hardware-dependent
test thresholds. Re-run on the target machine and measure sustained playback
including runtime snapshot publication via `editor-cell-playback-benchmark`.
GUI drawing remains excluded. The bridge benchmark also measures the
coupled source/phase/thermal path.

`station/CellThermal.h` supplies the sparse implicit heat solver: backward Euler
with exact star elimination (including parallel edges) or preconditioned CG for
general meshes. It uses preallocated linear-size storage, actual gas capacities
and no hidden mass on empty/unresolved nodes. Heat increments are recovered from
capacity times temperature change to avoid cancellation on trace nodes; an
energy-constraint adjustment on the largest-capacity body is bounded by the
convergence budget. Link-flux residuals and the adjustment are exposed. Nonlinear
Hull radiation reuses the compartment integrator. This native Cell solver does
not replace the standalone compartment thermal implementation.

## Source integration

```cmake
add_subdirectory(path/to/station-systems station-systems)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE StationSystems::StationSystems)
```

No parent include directories or dependency targets are required. The target
propagates C++20, public include directories, generic serialization and Lua
requirements (including the shared `station/support` script-adapter headers). Windows/MSVC and Linux builds
use the same sources and target. This is a source-consumed API, not a stable ABI
or an installed binary package contract.

```cpp
#include <station/Configuration.h>
#include <string>

station::StationSystemsSettings settings;
settings.enabled = true;
station::ResourceStoreSettings water;
water.id = "water";
water.unit = "kg";
water.capacity = 100;
water.startingQualified = 50;
settings.stores.push_back(water);
std::string diagnostic;
bool valid = station::validateStationSystemsSettings(settings, diagnostic);
```

Validation accepts a const configuration and returns false with the existing
human-readable diagnostic on refusal. Named references, finite numeric bounds,
material conservation, capacity, and all existing domain invariants remain in
force. Supplying the optional `brokenReferences` output selects the existing
incomplete-draft mode: unresolved references are recorded while other rules stay
strict. It is not the full validation required to start a simulation.

`station/StationConnections.h`, `station/StationPorts.h`, and
`station/StationPowerBuses.h` expose the shared configuration catalogues.
`station/AtmosphereMetrics.h` contains the atmosphere observation/metric records.
Composition helpers are private implementation details under `src`, not public
headers. Consumers include `station` headers and use the `station` namespace;
core Station forwarding headers and aliases have been removed. World
coordination, reference document wrappers, and presentation remain client-owned.

## Configuration persistence

```cpp
#include <station/Persistence.h>

std::string text = station::serializeStationConfiguration(settings);
station::StationSystemsSettings restored;
bool parsed = station::parseStationConfiguration(text, restored, &diagnostic);
```

Standalone text uses the existing root `schemaVersion` and `stationSystems` map.
Serialization validates complete settings and throws `serialization::SerializationException`
on refusal; parsing returns false with a diagnostic and leaves its destination
unchanged. No caller-supplied serializer is required. Clients own storage.

For embedding, `writeStationSystemsSettingsMap` and
`readStationSystemsSettingsMap` accept the World-independent
`serialization::Serializer` interface. Consumers implementing embedding backends
link `Promethium::Serialization` explicitly. YAML and binary use the same codecs
and historical schema gates; World retains its enclosing version. The reader's
explicit `validate = false` mode is for client wrappers that subsequently apply
the existing draft-tolerant validation, not for starting a runtime unchecked.
`stationConfigurationSchemaVersion` preserves the existing feature-dependent
version selection. Simulation-facing headers do not include backend headers.

World wrappers, reference-document descriptions/provenance, and editor diagram
positions, pan, zoom, and prefab grouping/provenance remain client-owned. Existing
editor document APIs continue to combine that metadata with the shared codecs.
The standalone settings API neither stores nor returns presentation metadata.

## Configuration authoring

`station/StationPorts.h` exposes connection eligibility (`evaluateStationLink`),
reference writes/clears, growable Port sets, and planned node rename/removal.
Eligibility combines structural Port descriptors with the existing configuration
validator, allowing incomplete drafts without admitting new semantic errors.
`applyStationLink` is the existing reference writer, not an eligibility gate:
clients evaluate first, then write an accepted link. Plans use stable authored
identities, not container positions; stale/refused transformations retain their
existing transactional behaviour and diagnostics.

`station/StationPrefab.h` exposes template/catalogue parsing, deterministic
namespaced expansion, exposed-Port resolution, coupled parameter edits, caps,
collision checks, and atomic whole-instance rename/removal:

```cpp
#include <station/StationPrefab.h>

station::PrefabTemplate prefab;
if (station::parsePrefabTemplate(templateText, prefab, &diagnostic)) {
    station::PrefabExpansion expansion;
    auto id = station::allocatePrefabInstanceId(prefab, settings);
    if (station::expandPrefab(prefab, station::defaultPrefabOverrides(prefab),
            id, expansion, diagnostic)) {
        station::applyPrefabExpansion(settings, expansion, diagnostic);
    }
}
```

Expansion output is ordinary configuration, with internal reference identities
and resolved exposed Ports. No World or diagram is constructed. Clients own
layout, visual grouping, and persisted presentation provenance; `PlacedPrefab`
is only the authoring boundary/override record, not simulation state. Parsing
retains the established reset-on-refusal output semantics; expansion and refused
edits leave their destinations unchanged. Validate a completed configuration
before runtime construction (an exposed endpoint may still need outside wiring).

## Runtime execution

```cpp
#include <station/StationSystemsRuntime.h>

station::StationSystemsRuntime runtime(settings, 1.0f / 60.0f);
bool advanced = runtime.advanceTo(60.0);
auto snapshot = runtime.snapshot();
auto diagnostics = runtime.diagnostics();
```

The base quantum and authored `systemsStepWorldTicks` retain their existing clock
contract: advancement commits whole Station steps until the target is reached
or exceeded. Compare results at matching committed `snapshot.elapsedSeconds`,
not equal call counts. Power, life support, material exchanges, thermal systems,
biology, conservation audits, faults and failure containment use the same engine
as World. `station/StationMetrics.h` and `station/StationHistory.h` provide the
observation catalogues and bounded history queries.

### Variable-duration updates

```cpp
station::StationSystemsRuntime runtime(settings, station::StationSystemsUpdateMode::Variable);
bool advanced = runtime.update(0.1); // exactly one Station update for 0.1 simulated seconds
advanced = runtime.update(1.0);      // next update covers 1 second; state is preserved
```

Select the drive mode at construction. `update(deltaSeconds)` is available only
in Variable mode and `advanceTo(targetSeconds)` only in Fixed mode; calls to the
wrong mode return false unchanged. Deltas must be positive, finite, no greater
than `float` maximum, and representably advance elapsed time. Invalid deltas
are refused without mutation; disabled runs do not advance. Script/audit failures
retire the run without advancing committed time, just as in Fixed mode. Existing
fixed-step clients retain count-derived time and whole-step target semantics.

Variable updates send the supplied delta to every lifecycle sandbox and the
same audited ledger. Event-driven subdivisions at schedule, profile, capacity,
and protection boundaries remain enabled: one client update does not promise
one indivisible physics calculation or constant execution cost. The tested Editor
range is 0.01–1 second; this is not blanket timestep-invariance certification.

Snapshots set `variableTimestep = true`, `systemsStepWorldTicks = 0` (no World
ratio), and `systemsTimestepSeconds` to the last committed delta (initially zero).
Step ids still count committed external updates. Monitor persistence uses elapsed
durations rather than sample count times the most recent delta. Observation
cadence is measured in simulated seconds: sample once at the first committed
endpoint reaching a due boundary, skipping crossed boundaries without inventing
intermediate states. Variable histories retain endpoint timestamps and step ids,
time-weight coarsened values, and report zero `sampleIntervalSeconds` and
`resolutionSeconds` to indicate irregular sampling; consumers must use timestamps.

Replay schema 139 adds Variable mode and a run-length-encoded journal of committed
update durations. Replay reproduces the exact step partition and command ordering,
not merely elapsed time; old fixed-step replay remains readable. No historical
step times are reinterpreted when a delta changes.

The Editor schedules nominally ten updates per real second, passing `0.1 * speed`
to `update`, independent of render frequency. It permits at most five catch-up
updates per frame after stalls/native dialogs and discards excess wall-time debt
rather than entering an unbounded catch-up loop. It remains synchronous, so heavy
updates can still stall the interface; a slower-than-10-Hz renderer cannot promise
precisely spaced wall-clock updates.

The default source is embedded from `resources/lifecycle.lua` in this project;
CMake tracks edits and regenerates the embedding on incremental builds. No file
lookup occurs at runtime. Pass an optional source string as the constructor's
third argument to select explicit, immutable per-runtime Lua instead. An empty
or malformed explicit source throws on construction; execution failures return
false from advancement and publish diagnostics, never silently fall back.
`bundledStationSystemsSource()` returns the default text for explicit adaptation.
The application's managed resource references this same authored file.

Runtimes own their state and accepted source independently. Externally serialize
all access to each runtime; no internal locking or asynchronous execution is
promised. Returned references are borrowed from the runtime and must not outlive
it; copy observations needed across mutations. Invalid construction throws;
refused live edits and commands return false with diagnostics without partially
applying the change. Frozen topology and initial-condition restrictions remain
unchanged. `writeFaultReplay`/`readFaultReplay` use the shared serialization
interface and authoritative configuration codec; replay reconstruction accepts
explicit source selection too. No backend headers enter the simulation API.

## Compressible atmosphere pathways

Every explicit `AtmospherePathwaySettings` uses pressure-driven ideal-gas bulk
flow for interior and exterior openings. `dischargeCoefficient` defaults to 0.7
and multiplies `areaM2 * openFraction`; it is live-editable and validated in
(0,1]. Schema 140 writes it; older configurations load with 0.7. There is no
legacy diffusion-only bulk mode. Residual molar counterdiffusion separately
mixes compositions, including at equal total pressure.

The upstream mixture sets gamma and specific gas constant. The flow is choked
below the critical downstream/upstream absolute-pressure ratio, subsonic above
it, and zero at equal pressure; a closed opening is sealed. Snapshot
`flowRegime`/`pressureRatio` describe the current state. `bulkFlowRateKgPerS`
is the signed A-to-B last-step average excluding mixing; existing per-species
rates include mixing. Enthalpy and bulk/species rates remain last-step means,
not instantaneous nozzle rates.

Pathway-connected atmospheres (even with closed Doors) use inventory-dependent
gas `sum(m cv)` and absolute internal energy `sum(m cv) T_K`; enthalpy crossing
an opening is `sum(dm cp) T_K`. No authored fixed capacity becomes hidden wall
heat mass. Standalone atmospheres retain their existing explicit-capacity
contract. Snapshots expose starting/current capacities, `inventoryThermodynamics`
and `inventoryEnergyKJ` for sensible energy carried by non-pathway inventory
changes; this term participates in the thermal audit. Hull conduction/radiation
and existing condensation/latent heat are separate.

Shared adaptive donor-limited substeps (maximum 2 ms, 1% donor mass per edge)
and pair-equilibrium bisection prevent overdraft and pressure overshoot. This
adds work inside an external update; a finite integration budget can refuse
pathological configurations. Positive numerical heat-capacity floors do not
add mass to the energy ledger. Hull splitting permits its existing 1e-7 kJ
energy error on vanishing-capacity nodes instead of demanding nanokelvin
agreement in trace gas. Stiff multi-contact meshes switch from pair splitting
to a simultaneous implicit, second-order extrapolated conduction solve, with
conservative per-edge bookings and rejection of negative absolute temperatures. Constant room-temperature species properties, the
well-mixed assumption, and unchanged low-pressure Hull U-values are engineering
approximations; jets, acoustic propagation, ice and real-gas effects are not
modelled. See [ADR-0003](../docs/adr/0003-compressible-door-gas-flow.md).

## World lifecycle integration

World and its core-owned SimulationCoordinator consume the public `station`
headers and runtime directly. World owns the immutable source selection and
passes it explicitly to each replacement runtime, including reset and replay.
No Station runtime depends on World, Sector, Agents, routing or traversal.

The coordinator still supplies `(World tick offset + next World tick) * World
quantum` to `advanceTo`; World movement retains its 60 Hz timing. Station may
remain ahead by less than one whole Station step. Pausing World makes no Station
calls and retains the current runtime. Reset reconstructs from authored settings
and the same source, clearing both clocks; disabling Station removes its runtime
without disabling World movement. Construction is preflighted before applying
configuration, preserving transactional source/validation failures. World
persistence adapters delegate to the shared codecs; the enclosing World schema
and replay drive offset are unchanged.

## Live edits, commands and replay

`updateNode<Settings>(id, mutator, &diagnostic)` selects an existing node by its
settings type and identity; an optional node-type string overload checks that
selection too. The callback receives a detached, scoped candidate, not live
runtime state. Do not retain references to it or re-enter the runtime from it.
Callback exceptions propagate without applying the candidate. Identity, frozen
endpoints/model opt-ins and initial-condition changes are refused whole, with a
diagnostic; successful no-op edits clear the diagnostic. Existing per-type
parameter validation and numeric handling remain unchanged.

`updateSettings(offered, &diagnostic)` applies a whole configuration's supported
parameters. All identity sets and frozen topology are preflighted before any
collection is changed. Unlike a typed mixed edit, this historical bulk contract
**preserves** initial fields while applying the parameter half. Persist
`effectiveSettings()`, not the offered settings, to retain the actual baseline.
Neither API resets accumulated inventories, temperatures or histories.

Operational commands are separate from observations:

- `injectFault`: validated, attributed request, booked at the next step.
- `resetTrip`: free, immediate latch clear; unresolved triggers can re-latch.
- `breakerReset`: explicit Power Bus latch reset, retaining Alert lifecycle.
- `restoreCondition`: rate-bounded recovery funded above the service-stock reserve.
- `repairFault`: reserved-parts maintenance with a returned job ID, full duration,
  isolation, interruption/restart and accounted completion waste. Never reverses Wear.

Refused commands return false with useful diagnostics and no partial state,
bookings or journal entry. Accepted live edits and operational commands are
recorded in order against committed Station step boundaries, including runs
without fault models. There are no mutable accumulated-state references.

```cpp
#include <station/Replay.h>
#include <station/StationSystemsRuntime.h>

std::string text = station::serializeStationReplay(runtime);
std::unique_ptr<station::StationSystemsRuntime> restored;
bool loaded = station::parseStationReplay(text, restored, &diagnostic);
// For an injected runtime, pass its chosen source as the fourth argument.
```

Standalone replay is self-contained YAML containing the effective settings,
base quantum, schema version and existing replay block. It delegates to the same
configuration and command codecs used by World's YAML/binary embedding; no
caller-supplied serializer or World schema is required. Clients own storage.
Parsing constructs a candidate runtime and leaves the destination unchanged on
malformed data, invalid source, refused commands or failed advancement. Explicit
Lua is selected at reconstruction, never serialized or silently replaced with
the default. As with World replay, Monitor observations restart on load; mandatory
breaker announcements retain their lifecycle. Replay reconstructs committed run
state, not external callbacks or failed-step diagnostics.

For existing embedding clients, `writeFaultReplay`/`readFaultReplay` remain the
same public codecs with explicit source selection and settings-schema inputs.
World persistence uses the public `station` headers directly; no core Station
compatibility API remains. Core owns
its enclosing version and `stationFaultDriveWorldTick`; the library owns the
embedded settings, elapsed Station steps and command journal. World preflights
configuration/source/replay before replacing live state and passes its immutable
client-selected source to both ordinary load and replay reconstruction. Existing
Fault, Condition and Wear replay opt-ins and the World-to-Station clock offset
are preserved without a new format or migration.

## Observation and reporting

The public runtime's `snapshot()`, `events()`, `trends()`, `metrics()`,
`recordingStatus()`, `observationRevision()`, `catalogue()`, `query()` and
`queryWork()` expose the editor's same reporting implementation without World
or presentation dependencies. `diagnostics()` reports execution failures;
`auditStationStability(snapshot, events, trends, diagnostics)` in
`station/StationSystems.h` performs the existing read-only finite/bounds,
conservation and history audit. It neither corrects state nor relaxes tolerances.

```cpp
auto revision = runtime.observationRevision();
auto chart = runtime.query("power"); // Copy: the next selection reuses the cache.
auto audit = station::auditStationStability(runtime.snapshot(), runtime.events(),
    runtime.trends(), runtime.diagnostics());
```

Snapshots and history exports are copies. Events, diagnostics, catalogue and
query references are borrowed; copy them before runtime mutation. In particular,
a different chart query replaces the single cached result even through const
access. Repeating an id at the same retained-history revision does not prepare
it again. Query work counters are diagnostics, not simulation state. The revision
identifies the run and retained history, not every physical change; snapshots
and audits never advance the clock or change that revision.

Chart definitions and metric descriptors declare canonical units, roles,
requested-versus-actual semantics and expected contributions. Snapshot cumulative
totals and last-step rates/amounts keep their own documented meanings; do not
interpret cumulative totals as chart rates. Missing, invalid, unaligned or
incompatible-unit contributions remain gaps/incomplete results, not zeros.
Battery charge is capacity-weighted, not an average of individual percentages.
`station/StationMetrics.h` also exposes catalogue construction and pure queries
on exported histories for clients that retain their own copies.

`station/StationHistory.h` exposes the bounded recorder and its limits. Default
bounds are 256 rows, 1024 metrics, 1 MiB metadata and 32 MiB recording memory;
admission can lower the row limit or refuse complete recording, never silently
select a subset of metrics. Histories coarsen on an aligned grid and expire by
retention duration; status reports resolution, retained range and truncation.
The event window retains the newest `StationEventHistoryLimit` events.

Monitors and Alerts retain ADR 0031 semantics: rules observe only, disabled rules
do not backfill, and Alert callbacks are code-bound announcements. Reporting
introduces no authored callbacks, actuation authority or presentation dependency.
No core Station forwarding headers are needed by reporting clients.

## Builds and verification (Release only)

Promethium consumes this repository at `ext/numina-space-industries` and
incorporates the project with `add_subdirectory`; normal
incremental builds require no separately launched Station build:

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --config Release -j 16
ctest --test-dir build-linux -C Release -j 16 --output-on-failure \\
  -R '^(station-|editor-|persistence-|world-|simulation-step-timing$|smoke-ownership-audit$)' \\
  -L 'validation-fast|ownership'
```

Run the application commands above from the Promethium root. Main-build
platform dependencies still apply (see its build scripts).
From the Numina root, for the independent library and public-only consumers, use:

```sh
cmake -S station-systems -B build-station -DCMAKE_BUILD_TYPE=Release
cmake --build build-station --config Release -j 16
ctest --test-dir build-station -C Release -j 16 --output-on-failure
```

`STATION_SYSTEMS_BUILD_TESTS` enables the public-target-only consumers (on by default
for an independent build, or when the parent enables `BUILD_TESTING`). It reuses
the established pathway fixture/refusal matrix with the World integration checks;
those checks retain their World transactional-refusal assertions. The authoring
consumer also reuses the established connection eligibility/growable-Port and
prefab parsing, expansion, dial, cap, rename, and cascade-removal scenarios via
public headers only. The runtime consumer shares the existing clock,
event-split, saturation, ordering, rollback and maintenance scenarios with the
integration runner, at unchanged committed times and tolerances. It also tests
embedded/injected source isolation, visible failures and binary runtime replay.
The command consumer shares existing live-edit, fault/outage, funded Condition,
Repair lifecycle/interruption and transactional-failure scenarios with the World
runner. It covers pending/in-flight/completed replay, Trip/breaker reset, ordinary
live-edit journals, frozen endpoint/identity refusals, callback exceptions,
malformed replay and explicit-source reconstruction through public APIs.
The reporting consumer shares battery arithmetic/gap/coarsening, bounded recorder
and Monitor timing scenarios with the World runner, and drives the existing
long-run workload through public snapshots, charts, events and stability audits.
It links only the public library target; fixture files are test inputs, not
runtime library assets. Presentation persistence checks remain in the existing
integration runner. The main CTest
suite also registers `station-library-independent-build`, which incrementally
configures/builds/tests this project without the application's CMake context.

### Scenario and headless-client migration (#958)

`station-scenario-consumer` compiles 50 established checks from seven additional
suites: invariance, physical fidelity/benchmark workloads, power connection
eligibility, rate-aware protection, runtime regressions, Shared supply and
Transfer supply. Their bodies live in `tests/scenarios`; the main smoke suite's
forwarding translation units reuse exactly those bodies and registrations.
Physical assertions, tolerances, durations and explicit per-runtime Lua injection
are unchanged. The independent runner supports `--list` and `--check <name>`.
`station-step-timing-consumer` also builds the existing timing utility check using
its public support header, without the main include directory.

The generic smoke runner/context and biology arrangements are shared test support.
Only the main client's fixture hooks install the application resource resolver;
the independent runner has no World/resource-manager hooks. Runtime reference
fixtures read settings through `parseStationConfiguration`, not a second codec.
Reference descriptions/provenance, World replay, document/layout persistence and
editor fixtures remain in the main integration suites. Test fixture YAML is not
required by the library's default Lua: `station-library-runtime` runs from an
asset-free working directory.

### Contracted-boundary verification (#959)

Every public header, including shared script support, compiles in its own
translation unit linked only to `StationSystems::StationSystems`. This catches
missing includes and dependency requirements hidden by the main build.
`station-library-boundary` rejects reverse includes, client private-header access
and retired core Station forwarding headers. The independent configure/build test
also prevents the parent's targets and include paths from masking dependencies.

Historical pre-import Linux Release verification with CTest `-j 16`:

- Full incremental main build (including editor, core and headless targets) passed.
- The main selection shown above: **596/596 passed**, including World/editor
  workflows, persistence, source selection, ownership and independent build.
- Independent library/public-only consumers: **58/58 passed**, including the
  long-run and benchmark scenarios, generic serialization and runtime replay.
- All public-header isolation compile checks passed in both builds.

No assertions, physical tolerances, document formats or simulation algorithms
were changed. This is not new timestep-invariance certification; ADR 0025 and the
existing scoped physics decisions still apply. Main exhaustive/non-fast lanes
and Windows were not verified; unchanged submodule test suites were not run.
Promethium ADR 0038 records the original dependency, resource and persistence
ownership decision. The Numina repository now owns this project, sibling
`serialization/`, shared scenarios/support and the required reference fixtures;
World/editor-specific integration code and assets remain in Promethium.
