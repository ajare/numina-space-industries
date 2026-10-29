# Systems bridge

Source-integrated C++20 mapping from Installation geometry to Station Systems
atmosphere compartments. Add this directory with `add_subdirectory` and link
only `nsi::systemsBridge`; its public headers and dependencies are transitive.
It can also be configured standalone. Core still depends only on serialization,
not on the bridge, simulation, or Lua.

```cpp
#include <nsi/systems/Atmosphere.h>

station::StationSystemsSettings settings;
settings.atmospheres = nsi::systems::expandInstallation(installation);
auto thermal = nsi::systems::expandThermal(installation);
settings.hull = std::move(thermal.hull);
settings.thermalLinks = std::move(thermal.links);
settings.solarCycle = std::move(thermal.solarCycle);
```

`expandSector` returns an optional compartment (invalid indices throw).
Void geometry or a Void atmosphere preset returns no compartment. Expansion
via `expandInstallation` returns only atmospheres. IDs are
`sector-<levelIndex>-<uid>`.

Earth-like is intensive: 101.325 kPa total, 20 °C, 40% RH, dry O₂ fraction
0.2095 and CO₂ fraction 0.0004. The spec's listed dry fractions total 0.9907;
because the simulation models only three dry species, the unmodelled 0.0093
balance is included in nitrogen, giving N₂ fraction 0.7901. Fractions therefore
sum to one without changing O₂ or CO₂. Water vapour consumes its share of the
total pressure **before** the dry gases are sized using the library's gas
constant and molar masses.

Every mass scales with `Sector::volumeM3()` (12 m³ per Cell per Level).
Thermal capacity uses 0.851 kJ/(K·m³), density × **cv**, not advected **cp**
(1.005 kJ/(kg·K)). Declared limits are 90–110 kPa total, minimum 16 kPa O₂,
and maximum 5 kPa CO₂.

Sector assignments pass through unchanged: occupant count and all four rates,
leak rate, reservoir id and exchanger conductance, and optional fire parameters.
A reservoir supplies heat-only coupling; an empty id leaves the compartment
adiabatic when no geometry-derived thermal link is present. Fire parameters enable the simulation's injectable `fire` fault;
they do not ignite a fire at startup. Declared limits report crossings without
clamping the integrated state.

`expandThermal` maps side exterior skin to one station-wide radiating Hull and
one drain per pressurised Sector with nonzero side area. Conductance is preset
U × area / 1000 in kW/K. Hull emitting area is the sum of linked areas, projected
area is one quarter, and panel heat capacity is 0.4 kJ/(K·m²) times that area.
Optical properties use the jointly calibrated core presets (ADR-0002). Solar
profile level is constant zero: absorbed power is exactly zero with positive
optical coefficients. No hull is emitted without a drain, and no hull choice
means no links. No atmosphere pathway is ever inferred. This tracer counts
side skin only; roof, underside and partition coupling are not implemented yet.

The runtime consumer follows the station-systems scenario-rig precedent and
checks isolated temperatures across ticks, active loads/couplings, and pressure
and gas-quality crossings. Editing-identity coverage is tracked by #9, and
domain records by #10.
