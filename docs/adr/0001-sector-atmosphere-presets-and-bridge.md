# Intensive Sector atmospheres through a separate systems bridge

Sector atmospheres use intensive presets with extensive values derived from geometry, so resizing a Sector cannot leave stale gas masses or thermal capacity. These decisions implement the agreed [Sector Atmosphere spec (#6)](https://github.com/ajare/numina-space-industries/issues/6); domain vocabulary is in [CONTEXT.md](../../CONTEXT.md).

**Status: Accepted, amended by [ADR-0002](0002-geometry-authored-thermal-coupling.md).** Its geometry-authored conduction and hull radiation decision supersedes the no-thermal-links rule and qualifies the inert-temperature guarantee below; the no-inferred-gas-pathways rule and dependency boundary remain in force.

## Presets and physical scale

Earth-like specifies composition, total pressure, temperature, humidity, limits and thermal properties, not per-Sector gas inventories. The bridge derives each gas mass from its partial pressure, temperature and Sector volume using the simulation's gas constants and molar masses; water vapour consumes its pressure budget before the dry gases are sized. Only three dry species are modelled, so the unmodelled dry-gas balance is folded into nitrogen to preserve total pressure without changing oxygen or carbon dioxide fractions.

One Cell has a 2 m × 2 m footprint and consecutive Levels are 3 m apart. The authoritative constants live in `core/include/nsi/core/Units.h`: an Interior Sector's volume is its Cell area count × height in Levels × 12 m³. This scale sizes every derived inventory and life-support quantity; it is not an arbitrary rendering scale.

**cv ≠ cp:** a fixed-volume compartment's volumetric thermal capacity is density × **cv**, approximately 0.851 kJ/(K·m³) for Earth-like air. Multiplying this by volume gives compartment thermal capacity in kJ/K. Advected specific heat is **cp**, approximately 1.005 kJ/(kg·K), and belongs only to the enthalpy of gas crossing a pathway; substituting cp for cv would incorrectly size the compartment's stored thermal energy.

## Void is absence

The Void atmosphere preset produces no compartment, not a compartment at zero pressure. A simulation compartment requires positive volume and positive pressure limits; the reserved `$exterior` endpoint instead has no inventory, volume or temperature. `SectorType::Void` is a separate geometric concept: no floor and no claimed volume. An Interior Sector may retain its floor while using the Void atmosphere preset, although authoring that choice in the Editor is outside this work's scope.

## No implicit heat paths (original decision, amended by ADR-0002)

**Sectors do not transfer heat between themselves.** The bridge emits no `ThermalLinkSettings`, including across shared Walls. A compartment has no conductive heat exchange unless its own authored assignment names a reservoir coupling; its own loads can still change its temperature. With inert loads and no coupling, its starting temperature remains unchanged.

Exterior heat loss is out of scope: vacuum conducts nothing and `$exterior` has no temperature to conduct against. Radiation to space follows a T⁴ law and belongs to the Hull model, not a fabricated conductive link. The bridge also emits no inter-Sector gas pathways: geometry currently has no authored door or hatch to expand, and any future gas advection must be explicit rather than inferred from adjacency.

## Dependency boundary

Core holds geometry and authored atmosphere references, loads and couplings; it remains free of the simulation and Lua. A separate `nsi::systemsBridge` module links `nsi::core` and `StationSystems::StationSystems`, owns the preset-to-`station::AtmosphereSettings` mapping, and originally returned atmosphere compartments only (extended to geometry-authored thermal settings by ADR-0002). Linking station-systems into core would pull in its public Lua dependency and reverse the Editor's geometry/simulation independence; the bridge keeps that dependency direction explicit.
