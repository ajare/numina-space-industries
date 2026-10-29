# Reset-time local structural topology (#27)

`nsi::systems::expandStructuralTopology` in
`<nsi/systems/StructuralTopology.h>` is an **opt-in candidate**, consuming #26's
`C1-v1-provisional` material presets and `station::ThermalBody` API. It implements
geometry expansion under [ADR-0007](adr/0007-native-structural-thermal-contract.md),
not the solver (#28), playback integration/rollback (#29), radiation cavities
(#30), Door pose changes (#31), or default rollout (#35). Existing bridge APIs,
standalone compartments, native playback defaults and YAML are unchanged.

```cpp
#include <nsi/systems/StructuralTopology.h>
auto topology = nsi::systems::expandStructuralTopology(installation);
```

## Geometry and ownership

- Expand Interior world-height claims, including claims outside intermediate
  Level grid bounds. Geometric Void has no floor or structural inventory.
  Interior with the Void atmosphere preset retains exactly the same structure;
  this builder never constructs gas inventory or interprets missing gas as space.
- Use existing 2 m Cell faces and 3 m Level-height bands. Each physical face is
  enumerated once in canonical negative/positive world-axis order. Horizontal
  faces exist only at actual floors or the top/bottom of a claim; there is no
  invented floor inside a tall room. Shared Walls/floors have one assembly.
- At horizontal side interfaces, the claiming Cell's authored Wall extends
  through its height. A present Wall on either adjoining claim supplies solid
  geometry; absent Walls on both sides leave an opening. This also resolves
  boundaries owned by different starting Levels/offsets. Door clear geometry
  occurs only in its authored starting band; bands above it remain solid.
  Same-Sector present Walls are still physical structure, even when another
  route joins the Cells around the Wall. Absent same-Sector faces are not panels
  or separate portals.
- Exterior panels use Hull material; solid interfaces between Interior volumes
  use Partition material. `hull:none` returns a completely empty topology.
  No Partition means no shared solid capacity/contact and no invented opening
  through a present opaque Wall/floor (the later cavity treats it as reflecting).
- Each patch has exactly two bodies, splitting **one** total
  `area × arealCapacity` inventory. Both start at 293.15 K with consistent energy.
  Hull retains 400 J/(m² K), shared partitions/floors 2000 J/(m² K). No capacity
  enters gas EOS quantities. The builder does not mutate the Installation.

Assembly IDs contain integer world origin, normal axis and part
(panel/frame/leaf), not Sector indices, names, gas IDs or output vector indices.
Surface IDs add a fixed negative/positive side. Ownership identifies the source
Cell of the volume claim by starting Level and world x/y. No owner means
**geometric exterior**, not an Interior Void atmosphere preset. Rebuild mapping
from these IDs at Reset: vector/body indices are ephemeral. Editing physical
geometry can of course remove/change a patch; no state migration across edits
is promised. `surfaceMaterial(side)` maps Hull inside/outside optics to the
canonical geometric ordering.

## Explicit finite solid contacts

Every panel has one through contact `G = area / R_solid`. Contacts between
panels require an **exact common geometric edge**, not nearby centres:

- Coplanar panels join equal-normal sides regardless of Sector identity.
  Each side gets half the sheet and seam budget. The route uses sheet
  `G_sheet = min(k*d) × edgeLength / (2 × centreDistance)` in series with
  `G_seam = min(junction) × edgeLength / 2`.
- At noncoplanar junctions, sides bounding the same geometric Interior Sector
  (or both geometric exterior) join with the finite half-seam conductance.
  This is a coarse edge-junction convention, not a detailed fastener/solid mesh.
  Coincident edges represent joined structure, including cross-Level seams;
  a gap represents no contact. Sector indices never become persistent IDs.
- Zero passive sheet/seam coefficients create no lateral route. There is no
  global Hull body, dense surface matrix or inferred proximity connection.

The frame represents fixed material around/above the aperture, with its outer
face edges supplying panel seams. It receives `6 - 1.8 = 4.2 m²` in the Door's
base band and the leaf receives 1.8 m², using the parent Hull/Partition material.
Above-aperture bands are ordinary 6 m² panels. The leaf has through conduction
and one finite 2 m hinge seam to the frame, split across sides; it is **not**
welded to all four surrounding edges. This is a documented coarse hinge/frame
approximation, not mechanical Door simulation.

`openings` is geometry metadata, **not active radiation contacts or gas paths**.
A Door entry is initially blocked by its retained leaf and records exactly
1.8 m², without hydraulic Cd. Missing-Wall openings record the full physical
6 m² per height band, including tall claims; that area emits no solid panel or
mass. Opening between Interior claims does not become exterior merely because
either lacks gas. Later tickets activate Door portals/relocate leaf surfaces
without deleting energy or reinitializing these bodies. No-Partition Doors may
still have clear-opening metadata, but have no invented leaf/frame mass.

## Checked construction limits

`StructuralTopologyLimits` is separate from the 128 gas-compartment constraint:

| Limit | Default |
|---|---:|
| Claimed world-height Cells | 262,144 |
| Physical assemblies (two bodies each) | 1,048,576 |
| Solid contacts | 8,388,608 |
| Conservative payload/scratch byte budget | 1 GiB |

Checked addition/multiplication refuses overflow. Claim counts/bytes are checked
before world-claim construction (including core validation's temporary claims).
A counting traversal checks assembly/body/edge/opening allocation sizes before
allocating geometry descriptors. Exact contact counts are then computed from
sorted edge scratch and checked **before allocating contacts or constructing
numerical bodies**. No partially built topology is returned on failure.

`predictedStorageBytes` is a conservative payload + scratch admission bound,
not measured process RSS: it allows 192 bytes per claim for map/validation
nodes, four edge records per panel, two bodies per panel, opening/assembly
records, and up to 26 solid contacts per panel. At most four panels meet a grid
edge; the bound includes same-cavity junction pairings and Door hinges. The
conservative byte policy may refuse a topology whose exact final payload is
smaller; it never silently drops gas-free structure. Allocator metadata, stack
and caller-owned Installation storage are not an RSS guarantee. Callers may
supply tighter limits; raising them is not performance/rollout approval.
Failures are `length_error` diagnostics prefixed `Structural topology`, with
separate claimed-Cell, assembly, solid-contact, storage-byte or size-overflow
reasons. Invalid Installation geometry still uses core's validation diagnostics.

## Validation

`nsi-structural-topology` exercises one/shared/tall/stepped/offset/negative-world
geometry, single-counted areas and capacities, no intermediate tall floors,
explicit local and cross-Level seams, disconnected structures, same-Sector
physical Walls, coplanar joints across Sectors, Hull optical side mapping,
Interior Void-preset inventory independence, geometric Void, no Hull/Partition,
internal/exterior Door frame/leaf budgets, tall missing-Wall apertures, stable
surface IDs after Sector-index reshuffles, deterministic initialized Reset,
passivity, duplicate-contact rejection and independent construction limits.

```sh
cmake --build build-linux --config Release -j 16
ctest --test-dir build-linux -C Release -j 16 --output-on-failure
```

This is component coverage, not a claim that the #22 integrated acceptance or
#35 performance gate is satisfied.
