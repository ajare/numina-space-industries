# Core

C++20 geometry in `nsi::core`. Include `<nsi/core/Installation.h>` (or
`<nsi/core/Core.h>`) and link `nsi::core`.

## Installation

`Installation(width, depth)` creates one Interior Level with Exterior perimeter
Walls. Default dimensions are 16×16. All geometry types are nested within
`Installation`: `FloorLayer`, `WiringLayer`, `PipingLayer`, `Cell`, `SectorType`,
`Sector`, `WallType`, `Wall`, and `Level`. Layer objects are empty extension points.
The Installation name defaults to empty. New Installations default to Hull type
`MultiLayerBlanket`; `setHullType` accepts `BareSkin`, `StandardInsulated`,
`MultiLayerBlanket`, or `std::nullopt` (no hull). `hullSpec` exposes the calibrated
U-value, emissivity, absorptivity and target flux from [ADR-0002](../docs/adr/0002-geometry-authored-thermal-coupling.md).
New Installations also default to Partition type `InsulatedPartition`.
`setPartitionType` accepts `BarePartition`, `ThermalBreak`, `InsulatedPartition`,
or `std::nullopt` (no partition coupling); `partitionSpec` exposes their provisional
U-values of 5, 1 and 0.3 W/(m²·K), respectively.
`sideExteriorAreaM2` sums a Sector's Exterior Wall lengths times its height in
metres, including internal Void boundaries. `exteriorSkin` additionally reports
roof and exposed floor underside area using world-coordinate volume coverage.

Levels expose read-only Cells, Sectors, Walls and bounds. Use Installation
commands (`setCell`, `toggleWall`, `setSector`, `setOffset`, `addLevel`,
`removeTopLevel`) for validated edits. A Cell references its Sector by integer
index; a Sector carries its containing Level index. Areas are cached Cell
counts, not calculated by scanning Cells on access.

Validation permits at most 128 atmosphere compartments across the Installation:
only Interior Sectors with non-Void atmosphere presets count. Void Sectors and
Interior Sectors using the Void atmosphere preset do not consume this budget.
Construction, loading and geometry edits reject an excess before simulation
construction, reporting the compartment count, maximum and how to reduce it.

Walls represent shared edges, with `width*(depth+1)` horizontal edges followed
by `(width+1)*depth` vertical edges. `horizontalWall(x,y)` and
`verticalWall(x,y)` return indices into this single array. Interior/Interior
edges can carry Interior Walls; Interior/Void or Interior/outside edges can
carry Exterior Walls. Void/Void edges cannot carry Walls. Cell painting updates
its four adjoining edges: Interior/Void and Interior/outside boundaries receive
Exterior Walls; obsolete Exterior Walls and Void/Void Walls are removed.
Still-valid authored Interior Walls are preserved. Unrelated Walls are unchanged.

Sectors are flood-filled connected components of same-type Cells across None
Walls. Changes rebuild components and cached areas. The largest surviving
fragment retains a Sector's name; other fragments inherit height but start
unnamed. Merges inherit the largest contributor's properties. Ties use the
lowest row-major Cell coordinate, independent of serialized Sector indices.

Interior height claims the footprint across consecutive existing Levels.
Overlapping Interior volumes, nonpositive heights, Void heights other than 1,
and claims above the highest Level are rejected transactionally. Signed offsets
are measured in Cells. Appended Levels start Void. Only the top Level can be
removed, retaining at least one; if removing it would truncate a lower Sector,
it is instead cleared to one unnamed Void Sector with no Walls, preserving
its dimensions and offset. `removeTopLevel()` returns false in that case.

## Persistence

`serialize()` emits version-4 YAML with name, explicit `hull` (`BareSkin`,
`StandardInsulated`, `MultiLayerBlanket` or `none`) and all Levels, including dimensions,
offsets, Cell Sector indices, empty layer maps, Sector name/type/height/uid,
Sector atmosphere preset and, when non-inert, its atmosphere loads. Cached Sector
areas and Sector-to-Level indices are omitted and rebuilt on load.
`deserialize()` accepts versions 3 and 4. Version 3 loads without a hull;
a missing `hull` in version 4 also permanently means no hull. Loading never
applies creation-time defaults. An explicit no-hull choice is written as `hull: none`.
The `partition` key follows the same rule: missing (or version 3) means no
partition coupling; a deliberate opt-out is written as `partition: none`.
Named version-4 values are `BarePartition`, `ThermalBreak`, and `InsulatedPartition`.
It checks membership/topology, dimensions, Wall classification and
vertical occupancy. Invalid input throws `serialization::SerializationException`
and leaves the Installation unchanged. Old name-only and editor deck-row formats
are intentionally unsupported. Unknown fields are ignored, not preserved.

## Standalone build and test

An installed yaml-cpp package is reused when available; otherwise the pinned
version is fetched on first configuration.

```sh
cmake -S core -B build-core -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release -j 16
ctest --test-dir build-core -C Release -j 16 --output-on-failure
```
