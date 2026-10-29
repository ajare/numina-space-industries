# Geometry-authored thermal coupling and a radiating Hull

**Status: Accepted.** This decision amends [ADR-0001](0001-sector-atmosphere-presets-and-bridge.md) for [exterior heat loss and Sector thermal diffusion (#11)](https://github.com/ajare/numina-space-industries/issues/11). Vocabulary is defined in [CONTEXT.md](../../CONTEXT.md).

ADR-0001 bundled two different rules: no inferred gas pathways and no geometry-authored thermal links. We retain the first absolutely and reverse the second. Without interior conduction, only perimeter Sectors with exterior walls could lose heat; buried rooms would remain at their starting temperature forever regardless of the cold rooms surrounding them. Conducting through authored structure lets the core cool through its neighbours toward the exterior instead.

## Authored geometry permits conduction, never gas exchange

When a hull is present, the systems bridge is responsible for emitting `ThermalLinkSettings` through authored geometry: a shared Wall, a shared floor across Levels, and a Sector's own exterior skin. These are physical surfaces in the Installation, not inferred connections based on proximity. The Installation-wide Partition type sets the U-value of shared Walls and floors; the Hull type sets the exterior assembly's U-value and radiative properties. Conductance is U-value multiplied by the corresponding surface area, with units converted at the simulation boundary.

**No inferred gas pathway may ever be authored.** These links exchange heat by conduction only: no gas, composition or moisture passes between Sectors, and Sectors remain containment boundaries. The bridge emits no atmosphere pathways for this feature under any circumstance. Any future gas advection must require an explicitly authored door or hatch, never mere adjacency.

Each atmosphere compartment with exterior skin conducts to one Installation-wide Hull node. Exterior skin includes side, roof and exposed floor-underside area facing void, including internal voids. The Hull radiates to deep space following a fourth-power temperature law; vacuum is not a conductive medium, and `$exterior` is not a temperature reservoir. There is no fabricated conductive link to `$exterior`. Solar gain is out of scope; the solar profile is constant zero while absorptivity remains a positive preset property for future use.

## The inert-temperature guarantee is conditional on no hull

The previous guarantee — "with inert loads and no coupling, a compartment's starting temperature remains unchanged" — is **superseded whenever a hull is present**: authored geometry can now supply thermal coupling without a per-Sector reservoir assignment. This is not a claim that every Sector must cool; a Sector with neither exterior skin nor linked neighbours remains thermally isolated.

For **Installations with no hull**, the original guarantee still holds exactly: the bridge emits no thermal links, the Partition type has no effect, and inert loads with no coupling leave starting temperature unchanged. Old saved Installations do not acquire a hull implicitly; a missing hull property means no hull, not the new-Installation default. Sectors with the Void atmosphere preset still produce no compartment and no thermal links.

## Preset provenance

As with the existing lighting efficiency bands, these are **provisional engineering assumptions calibrated for plausible-range behaviour, not manufacturer-sourced figures**. They are balancing inputs, not validated product specifications.

Hull presets are calibrated jointly as (U-value, emissivity, absorptivity) triples against a target heat flux. Emissivity sets the equilibrium skin temperature, which sets the temperature difference across the insulation and therefore the conducted loss. Combining a published effective U-value, already including radiative resistance, with independently chosen emissivity would double-count that resistance. The flux column records each preset's calibration target, not a constant heat output for every operating condition.

| Hull type | U-value (W/(m²·K)) | Emissivity | Absorptivity | Calibrated flux (W/m²) |
|---|---|---|---|---|
| `BareSkin` | 8 | 0.80 | 0.40 | 224 |
| `StandardInsulated` | 0.6 | 0.85 | 0.20 | 62 |
| `MultiLayerBlanket` (new-Installation default) | 0.05 | 0.03 | 0.15 | 3.8 |

| Partition type | U-value (W/(m²·K)) |
|---|---|
| `BarePartition` | 5 |
| `ThermalBreak` | 1 |
| `InsulatedPartition` | 0.3 |

The interior convective film limits a real wall assembly facing air to roughly 10 W/(m²·K), so no hull preset exceeds that bound. Future rebalancing should revisit the coupled calibration rather than substituting a manufacturer's U-value in isolation.

## Dependency boundary is unchanged

Core owns authored Hull type and Partition type references, their intensive properties and derived exterior skin geometry, without simulation or Lua dependencies. The systems bridge owns their expansion into simulation settings using existing thermal-link and Hull primitives. The Editor owns authoring and display. This amends the bridge's atmosphere-only output rule, not the dependency direction established by ADR-0001; runtime implementation is tracked by the remaining tickets under #11.
