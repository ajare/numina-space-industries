# Structural Editor observations (#33)

## Opt-in and render modes

The left panel's **Thermal backend (resets run)** selects Geometry (unchanged
default) or LocalStructure (candidate). It is disabled while playing; changing it
explicitly performs a full Reset, clears history/time and initializes bodies.
It is Editor state, not YAML. This is not default-rollout approval: physical
presets and the serial performance review remain gated by #35.

**Thermal (gas)** retains gas-only temperatures and its grey unresolved state.
**Pressure** remains gas pressure. **Structure (surfaces)** colours each Cell
using the area-weighted temperature of all exposed sides currently owned by that
source Cell: side walls, floor, ceiling and parked Door surfaces. It is neither
one particular roof surface nor an equilibrated room temperature. The same
adjustable Celsius scale is used. No surfaces under an active structural backend
are grey; with no matching structural runtime the Default geometry colours are
retained. Interior Void-preset Sectors can have warm measured surfaces without
any gas measurement. Evacuation does not turn solid temperatures into air data.
Tall claims retain their physical source-Cell ownership.

Render/inspection controls do not alter authored geometry, optional wind/force
settings, gas EOS or save files. Backend changes are explicit Reset operations,
never in-flight substitution. Standalone compartments remain unchanged.

## Selection and readouts

In Sector mode select a Cell, then expand **Structure / real thermal topology**
in the Sector panel. Choose Cell or whole-Sector inspection. Both report separate
all/floor/ceiling/side area-weighted temperatures and min/max. Exposed area,
allocated side capacity (J/K) and stored energy (J, Kelvin reference) are separate
from gas-only capacity and gas energy in the Thermal tab. Shared assemblies have
one physical mass split between sides: a Sector's exposed-side capacity is not
the total mass of every neighbouring assembly. Global unique solid energy and
gas internal/kinetic energy are labelled separately.

The global heat-path tree shows last **accepted whole-call mean W** and cumulative
J for gas into solids, solid conduction magnitude, interior radiation magnitude,
exterior portal escape, exterior solid radiation and absorbed solar. These are
not selected-Sector rates. Internal magnitudes are not net energy losses;
interior radiation counts both surfaces of an internal exchange. Revision,
committed time, energy residual and cavity closure diagnostics are shown.

LocalStructure does not display legacy U-value gas-to-gas/Hull links, drain
indicators or their Systems graph as though they were the active network. There
is no whole-structure cooldown prediction. Geometry playback retains its legacy
local C/G response diagnostics, explicitly not nonlinear cooling predictions.

## Bounded topology graph

The surface browser reads pages of at most **16 selected Cell/Sector sides**.
Pick a side to show its stable geometry/axis/part/side ID, instantaneous
measurement, gas-only capacity/contact and cavity endpoint. The one-hop diagram
shows its gas film (if modelled), radiation endpoint and up to 16 actual immediate
solid contacts, with stable endpoint IDs, conductances and temperatures below.
Lines indicate connections, not flow arrows. It never follows contacts
recursively. New Cell selection resets stale page/side selection.

Up to 16 cavity portal edges are listed separately with both endpoints, physical
area and signed instantaneous A-to-B power. Every bounded query displays shown
and total counts; oversized immediate adjacency/portal lists explicitly remain
truncated, while surface pages can be navigated. No whole-Installation graph is
serialized or recursively drawn. This consumes the committed indexed APIs in
[structural-observations.md](structural-observations.md).

The radiation model is **visibility-free, diffuse well-mixed Sector cavities**:
no detailed view factors, intra-Sector visibility or occlusion. Cavity fields are
algebraic irradiance, not stored-energy nodes. Doors use **binary poses**: opening
parks both leaf sides at the deterministic Interior endpoint, preserves energy
and exposes clear 1.8 m² portal area without gas Cd. It is not hinge simulation.
After a Door control, irradiance/power remains unavailable until the next physics
commit rather than showing a stale value; surface temperatures remain valid.

## Histories and lifecycle

The graph always identifies its history as the selected **Sector's exposed
surfaces**, even when live inspection is Cell-local. Six series show area mean,
floor, ceiling, sides, minimum and maximum. All geometric cavities are sampled,
including Interior Void-preset Sectors; selection does not enable their physics
or history. Sampling is at most every 0.1 simulation seconds with a rolling
60-second window, not frame/wall time. Accelerated steps use committed simulation
time. Missing groups are NaN gaps, never zero/cold measurements. Pause and
refusal append nothing; Reset/document retirement clears histories. Gas histories
remain separate and unresolved gas is still a gap.

## Validation

`editor-structural-view` covers offset/unsorted owner selection, bounded pages and
actual incident edges, separate gas/solid/pressure colours, unresolved/missing
gas, no inactive legacy cooldown graph, directional/min/max histories,
simulation-time cadence/acceleration/rolling retention, pause/reset/backend
Reset/document retirement, optional diagnostics, authored save immutability,
Door aperture/power invalidation, lifecycle and thermal refusal retention, and
headless ImGui/ImPlot Cell/Sector selection/rendering with six plotted series.
Existing Editor rendering, atmosphere, playback and thermal panel tests remain
in the Release suite. Final incremental Release build and all **138 CTest tests**
passed with `-j 16` (161.97 s); all 14 Editor tests also passed in a focused run.
Whitespace checks passed. Full performance/GUI cost matrix and rollout are #35.
