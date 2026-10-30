# Orbital period, not planetary rotation, drives hull exposure

**Status: Accepted.** Recorded for [#36](https://github.com/ajare/numina-space-industries/issues/36), which is authoritative for the full specification. Vocabulary is defined in [CONTEXT.md](../../CONTEXT.md). This extends [ADR-0002](0002-geometry-authored-thermal-coupling.md), which explicitly put solar gain out of scope and left the solar profile at constant zero, and operates inside the ownership boundaries set by [ADR-0007](0007-native-structural-thermal-contract.md). It changes no code: it records the decisions that the implementation must honour.

The Hull radiates toward deep space and, in reality, absorbs the dominant heat input it will ever see from its star. To model that as a time-varying exterior boundary condition we had to choose what the author actually authors. We decided the input is the Installation's **inertial Orbital period**, together with orbital radius, **Stellar irradiance** at the planet, **axial tilt** and planet **Albedo** — and that the planet's rotation period is deliberately **not** an input at all.

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

Nadir lock is a fixed convention, not an authored parameter: **+z = zenith**, **+x = along-track**, **+y = cross-track**, which is also the orbit normal and so is fixed. The sun sweeps in the x–z plane at the orbital rate with a constant y component of `sin(tilt)`. t = 0 is **Local noon**, with the sunlit arc centred on it and an authored phase offset for tests. A second authored attitude rotation would have been needed for tumbling or sun-tracking; we declined it to keep the input count at one.

## Planet shine is modelled, and folded into the existing interface

Ignoring the planet would leave the eclipse sink at 3 K. In an ISS-like orbit that is off by a factor of the whole heat budget: a nadir-facing **Structural surface** has a view factor of 0.880 to the planet and receives roughly 210 W/m² of planetary infrared plus 359 W/m² of reflected sunlight — about 42% of full sun, and during eclipse the only external heat available. In geostationary orbit the same figure is about 15 W/m² and genuinely ignorable. The two regimes are not interchangeable and the model must be right for whichever the author picks.

The view factor is the planar-element-to-sphere result, `(Rp/r)² × max(0, −n · ẑ)`, **not** the sky-coverage fraction `1 − cos(alpha)`. The two differ by ~35% at ISS altitude and using the wrong one understates the LEO heat load. Under the nadir-lock convention this also means vertical side faces receive no planet shine at all, which is correct for a planar element and worth knowing before it reads as a bug.

The planet's infrared temperature is **derived** from energy balance, `(I × (1 − A) / 4σ)^(1/4)`, from a single authored Albedo, rather than authored separately. A dimmer star then cools the planet automatically instead of leaving a glowing world orbiting a dead one. Reflected sunlight is gated to zero during eclipse while planetary infrared continues, because the ground beneath an eclipsed station is the planet's own shadowed side and there is no sunlight there to reflect. Without that gate the station absorbs hundreds of watts of reflected sunlight while sitting in total shadow.

## The station-systems numerical seam is deliberately not a seam

`NativeThermalExterior` keeps exactly its two existing fields and the whole orbital model folds into them:

- Planetary infrared becomes an **effective Deep-space sink**, `T_eff = [(1 − F)·T_space⁴ + F·T_planet⁴]^(1/4)`. A grey body absorbs planetary infrared with its emissivity, so this is exact rather than an approximation, and a planet-facing surface radiates toward roughly 247 K during eclipse instead of 3 K with no interface change.
- Reflected sunlight becomes part of the absorbed solar term, because it is absorbed by *solar* absorptivity, which is not the emissivity governing the sink and therefore cannot live inside `T_eff`.

This is the minimum-seam choice. The accepted solver, its work budgets, its nonlinear iteration contract and its residual reporting are untouched, so this feature cannot perturb them. The whole feature presents through one new pure, allocation-free exposure evaluator in the bridge, tested analytically without a solver in the way, plus the existing transport and Editor playback seams.

## Authored as axial tilt in degrees, not Beta in radians

**Amendment.** The angle between the star and the Installation's orbital plane was first authored as the astronomy term **Beta angle**, in radians. That name is opaque to everyone who has not studied orbital mechanics and the radian unit made the control unusable — `1.5707` communicates nothing. The authored field is now **`axialTiltDeg`**, in degrees, and the model adopts two conventions that make the astronomy term unnecessary rather than merely renamed:

- **The Installation orbits the planet's equator.** The orbital plane is the equatorial plane, so the orbit normal is the spin axis and the star's elevation out of the orbital plane is the latitude it sits directly over.
- **The tilt is held at solstice.** The planet's lean points at the star permanently; it never swings away. So that latitude is constant and equals the authored tilt.

Under those two conventions `tilt ≡ Beta` exactly — no conversion, no second input, and every formula in the exposure evaluator is unchanged. What the author types is a world property they can picture (0° = star over the equator, 23.44° = Earth-like), and the geometry the model needs follows. The cost is stated plainly below: this is a permanent solstice, not a year.

`betaAngleRad` is refused on load rather than read as a zero tilt, so a stale save cannot silently re-tune the sky it describes. The evaluator converts degrees to radians internally and exposes `axialTiltDegrees()`; nothing outside core sees radians.

**The phase offset was renamed the same way, to `phaseOffsetPercent`** — a percentage of the Orbital period rather than a raw number of seconds. 0% is Local noon and 50% is local midnight, so it reads as a position on the cycle instead of a duration the author has to divide by a period they also typed. It also stays where it was meant to be when the Orbital period is edited, which a stored second count would not. The evaluator folds `fmod(percent, 100)` into turns — reduced before the division, because a huge authored value has no fractional bits left after it — and still exposes a derived `phaseOffsetSeconds()` for readouts that want the clock value. The old `phaseOffsetS` key is refused on load rather than reinterpreted, since reading seconds as a percentage would put the run somewhere else in the cycle than the author wrote.

**The Editor now reads that percentage as an angle.** The phase offset widget is an angle slider over one turn — 0 deg is Local noon, 180 deg local midnight — because a position on a cycle is easier to wind to than a number of percent, and the fold that makes 370 deg the same start as 10 deg is the same fold the evaluator already applies. Storage is unchanged: the document still holds `phaseOffsetPercent`. The conversion lives in the editor's shared orbit display module (`phaseAngleFromOffsetPercent` / `offsetPercentFromPhaseAngle`), which folds to one turn before scaling and scales by 360 over 100 rather than by 3.6, since 3.6 is not representable in binary and would not return the value it was given. The Orbit summary reads the same angle, with the seconds it corresponds to in brackets.

**The authored sky is presented as two groups, not one.** Stellar irradiance, Planet albedo and Axial tilt go under **Star and planet**: they describe the world and hold whatever path the Installation takes. Orbital period, orbit radius and the phase offset go under **Orbit**: they describe the ride. The split is presentational — one `Orbit` record, one `Author an orbit` switch over both, one evaluator behind everything — and exists because the two groups change for different reasons.

**The planet radius is authored, and the eight planets are presets.** Until this change the model had exactly one world: `PlanetRadiusM`, Earth-like and compile-time. That constant was never decoration — it is the radius of the shadow cylinder and the `(Rp/r)²` of the planet-shine view factor, so a wide world drawn with an Earth-sized occluder is not a slightly wrong sky but a different one. A "Jupiter" preset built over irradiance, albedo and tilt alone would have computed an Earth's shadow about a Jovian orbit. `Orbit::planetRadiusM` therefore joins the record, defaulting to `PlanetRadiusM` so a document written before the field existed loads as the ground it actually orbited: no migration, no reinterpretation, and no stale save quietly moved off the world it was describing. It must be positive and below the orbital radius — a Hull buried inside its own planet has no sky, and is refused rather than rounded into a permanent eclipse.

The catalogue lives in `core/include/nsi/core/WorldPresets.h` and carries the four quantities the model reads, from the NASA NSSDCA Planetary Fact Sheet. Four things about it are deliberate:

- **The Sun is not a world.** In this model the star and the orbited body are separate: the star lights the scene, and the world occludes it, reflects it and re-radiates it. The Sun has no albedo to author and no shine to give that is not simply the star again, so there is no honest row for it. What every row does carry is our Sun, at that world's distance — which is the only way the Sun appears in a catalogue of worlds at all.
- **Deep space is the absence of the record, not a row of zeros.** The control offers **Deep space** — no star and no planet — and it clears the `Orbit` entirely, which is the state the model already carries for a dark sky: no stellar gain, no occlusion, no planet shine, the hull left radiating to the deep-space sink alone. It is deliberately *not* an authored irradiance of 0 with a radius of 0, because a world with no radius is not a world and a star with no light is not a star; both are refused on the way in, so "nothing" cannot be spelled with zeros and then mistaken for a valid sky. The World control also stays live when the rest of the group is locked, because otherwise Deep space would be a place an author could get stuck in.
- **Published obliquity is folded, not copied.** Retrograde worlds run past 90° — Venus at 177.4°, Uranus tipped onto its side at 97.8° — and this model's tilt is the latitude the star sits directly over at solstice, which cannot leave `(-90, 90)`. Past the pole the latitude comes back down the other side, so the fold is `180 − obliquity`: Venus 2.6°, Uranus 82.2°. The table stores the fold *applied* rather than somebody's arithmetic, because `180 − 177.4` is `2.5999999999999943` and not the literal `2.6`; a hand-folded table would not match itself on reload.
- **The default record is Earth-sized but is not Earth.** Its radius is Earth's, and its irradiance is, but its tilt is zero — the deepest eclipse the model hands out, so a new document starts pessimistic and the author relaxes it on purpose. It therefore reads as *Custom* rather than as a world, which is the honest label: calling the worst-case sky "Earth" would misreport how much eclipse the author was starting with.

Applying a preset moves the world and leaves the ride alone — Orbital period and phase offset are untouched, because which world you are about does not decide how fast you travel around it. One thing has to follow: if the new world would swallow the current orbit, the orbital radius moves out with the ground and keeps the altitude above the surface the author already had. Refusing instead would leave them unpicking a radius by hand only to arrive back at the number they started from.

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

**No seasonal variation of the tilt.** Full seasons need the planet's year, its orbital phase and a declination that swings between the solstices — a second orbital model. The tilt is one authored constant held at solstice, defaulting to 0 for the worst-case eclipse. Note that the tilt is not only an eclipse knob: at zero tilt the cross-track faces never receive direct sunlight at all, and the nadir-facing underside never receives it at any tilt.

**No hull or partition preset rebalancing.** The C1 presets were calibrated at zero solar gain and a real sun will move every hull's equilibrium temperature. Re-deriving them is its own calibration ticket under its own gate discipline.
