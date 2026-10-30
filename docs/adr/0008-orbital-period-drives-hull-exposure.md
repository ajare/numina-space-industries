# Orbital period, not planetary rotation, drives hull exposure

**Status: Accepted.** Recorded for [#36](https://github.com/ajare/numina-space-industries/issues/36), which is authoritative for the full specification. Vocabulary is defined in [CONTEXT.md](../../CONTEXT.md). This extends [ADR-0002](0002-geometry-authored-thermal-coupling.md), which explicitly put solar gain out of scope and left the solar profile at constant zero, and operates inside the ownership boundaries set by [ADR-0007](0007-native-structural-thermal-contract.md). It changes no code: it records the decisions that the implementation must honour.

The Hull radiates toward deep space and, in reality, absorbs the dominant heat input it will ever see from its star. To model that as a time-varying exterior boundary condition we had to choose what the author actually authors. We decided the input is the Installation's **inertial Orbital period**, together with orbital radius, **Stellar irradiance** at the planet, **Beta angle** and planet **Albedo** — and that the planet's rotation period is deliberately **not** an input at all.

## The planet's rotation drives nothing

The planet's shadow is a cylinder pointing directly away from the star, fixed in the inertial frame. A planet spinning underneath its own shadow does not move that shadow. Eclipse timing, and the sweep of sunlight across the hull under **Nadir lock**, are therefore set entirely by the Installation's own orbital rate. The rotation period is a red herring for this feature.

This is the single most counter-intuitive decision here and the one most likely to be "fixed" by a future contributor who assumes a rotation period is missing. It is not missing; it is irrelevant.

The rejected alternative was the intuitive one: author the Installation's **speed relative to the planet**, with zero meaning geostationary. It fails on its own terms. A relative measure does not remove the rotation period from the model, it makes that period an invisible intermediate that must be added back before use — and the number you get is not the number the physics wants. For an ISS-like orbit the period of relative motion against the surface is 5,955 s while the orbital period that actually drives the thermal cycle is 5,570 s. Authoring the relative figure and using it as the cycle would drift eclipse entry by ~7%, roughly a minute per orbit. Linear speed is worse: it needs the radius to convert, making two authored numbers where one would do, with no gravity model present to keep the pair honest.

**Geostationary is a displayed condition, never a mode.** It is simply the case where Orbital period equals the planet's sidereal rotation, surfaced as a badge beside a derived **Ground drift** readout. Making it a mode would reintroduce the coupling we just removed.

## Orbital period and radius are independent

No Kepler, no gravitational parameter, no planet mass. The thermal model needs none of them, and deriving a radius we could have typed adds a core dependency for zero thermal benefit. The author may therefore write a combination that no gravity would permit. That is accepted: this is a balancing model, not an orbital simulator.

## Sunlight is per surface, from that surface's own normal

The previous lumped weight applied `absorptivity × area / 4` identically to every exterior contact, so there was no lit side and no dark side. We decided to compute direct flux per **Structural surface** as `max(0, n · s) × α × I × (1 − eclipse)`, using the outward normal the structural topology already records as an assembly axis and side. At most one half of the Installation is in direct sunlight at any instant, which is a geometric fact rather than an assumption, and each surface gets its own sunrise and sunset.

The ¼ factor was an isotropic Cauchy average. The per-face model replaces that average with true instantaneous geometry; for a box-shaped Installation the total absorbed power legitimately differs from the lumped value rather than matching it. Nothing is invalidated, because the C1 Hull flux anchors of [ADR-0002](0002-geometry-authored-thermal-coupling.md) were all calibrated at **zero** solar gain.

Nadir lock is a fixed convention, not an authored parameter: **+z = zenith**, **+x = along-track**, **+y = cross-track**, which is also the orbit normal and so is fixed. The sun sweeps in the x–z plane at the orbital rate with a constant y component of `sin(beta)`. t = 0 is **Local noon**, with the sunlit arc centred on it and an authored phase offset for tests. A second authored attitude rotation would have been needed for tumbling or sun-tracking; we declined it to keep the input count at one.

## Planet shine is modelled, and folded into the existing interface

Ignoring the planet would leave the eclipse sink at 3 K. In an ISS-like orbit that is off by a factor of the whole heat budget: a nadir-facing **Structural surface** has a view factor of 0.880 to the planet and receives roughly 210 W/m² of planetary infrared plus 359 W/m² of reflected sunlight — about 42% of full sun, and during eclipse the only external heat available. In geostationary orbit the same figure is about 15 W/m² and genuinely ignorable. The two regimes are not interchangeable and the model must be right for whichever the author picks.

The view factor is the planar-element-to-sphere result, `(Rp/r)² × max(0, −n · ẑ)`, **not** the sky-coverage fraction `1 − cos(alpha)`. The two differ by ~35% at ISS altitude and using the wrong one understates the LEO heat load. Under the nadir-lock convention this also means vertical side faces receive no planet shine at all, which is correct for a planar element and worth knowing before it reads as a bug.

The planet's infrared temperature is **derived** from energy balance, `(I × (1 − A) / 4σ)^(1/4)`, from a single authored Albedo, rather than authored separately. A dimmer star then cools the planet automatically instead of leaving a glowing world orbiting a dead one. Reflected sunlight is gated to zero during eclipse while planetary infrared continues, because the ground beneath an eclipsed station is the planet's own shadowed side and there is no sunlight there to reflect. Without that gate the station absorbs hundreds of watts of reflected sunlight while sitting in total shadow.

## The station-systems numerical seam is deliberately not a seam

`NativeThermalExterior` keeps exactly its two existing fields and the whole orbital model folds into them:

- Planetary infrared becomes an **effective Deep-space sink**, `T_eff = [(1 − F)·T_space⁴ + F·T_planet⁴]^(1/4)`. A grey body absorbs planetary infrared with its emissivity, so this is exact rather than an approximation, and a planet-facing surface radiates toward roughly 247 K during eclipse instead of 3 K with no interface change.
- Reflected sunlight becomes part of the absorbed solar term, because it is absorbed by *solar* absorptivity, which is not the emissivity governing the sink and therefore cannot live inside `T_eff`.

This is the minimum-seam choice. The accepted solver, its work budgets, its nonlinear iteration contract and its residual reporting are untouched, so this feature cannot perturb them. The whole feature presents through one new pure, allocation-free exposure evaluator in the bridge, tested analytically without a solver in the way, plus the existing transport and Editor playback seams.

## Exposure is opt-in at the wire level

**Compatibility amendment ([#48](https://github.com/ajare/numina-space-industries/issues/48)):** the original three-state wording in this ADR and #36/#40 incorrectly made a missing `Orbit` suppress even an explicitly supplied profile. That contradicts #36's existing-profile compatibility promise and pre-orbit native behaviour. We preserve compatibility rather than require otherwise irrelevant Orbit data: `solarCycle` takes precedence regardless of Orbit presence.

| Orbit | `solarCycle` | Behaviour |
|---|---|---|
| Absent | Absent | Dark sky, zero stellar gain |
| Present | Absent | Computed orbital exposure |
| Absent | Present | Legacy lumped profile heating |
| Present | Present | Legacy lumped profile heating; orbital model suppressed |

The profile owns the entire stellar term: `sum(absorptivity × area / 4) × hull irradiance × profile level`, with the authored bare Deep-space sink, no per-face cosine and no Planet shine. Open Doors retain the legacy rule of zeroing their whole face's solar weight. A zero profile remains an explicit dark override. `expandThermal` retains its zero-profile placeholder for no-Orbit Hulls so standalone compartment consumers stay dark too; native transport honours it like any other profile. No-Hull remains an absolute opt-out. Successful-step allocation and whole-call rollback guarantees are unchanged. No saved-data migration is needed.

A missing `Orbit` still means no orbit, never a default one — mirroring [ADR-0007](0007-native-structural-thermal-contract.md)'s rule that a missing Hull property means no Hull rather than the new-Installation default. New Installations do author an ISS-like orbit by default, following the `MultiLayerBlanket` precedent. Isolated dark-sky experiments must clear the Orbit and omit or zero the profile, rather than expect an explicitly supplied heating profile to be ignored.

## Deliberate omissions

**No inter-panel self-shadowing.** An L-shaped Installation's inner faces will receive sunlight they physically should not. Accepted and documented as a convexity assumption, in the same manner as the diffuse radiation cavity's documented occlusion limitation. Occlusion is a per-face visibility problem per step that would not survive the serial performance budget for fidelity nobody will notice.

**No seasonal Beta variation.** Full seasons need the planet's year, axial tilt and orbital phase — a second orbital model. Beta is one authored constant, defaulting to 0 for the worst-case eclipse. Note that Beta is not only an eclipse knob: at Beta = 0 the cross-track faces never receive direct sunlight at all, and the nadir-facing underside never receives it at any Beta.

**No hull or partition preset rebalancing.** The C1 presets were calibrated at zero solar gain and a real sun will move every hull's equilibrium temperature. Re-deriving them is its own calibration ticket under its own gate discipline.
