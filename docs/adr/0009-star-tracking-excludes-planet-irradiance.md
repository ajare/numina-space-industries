# Solar arrays track the star, and the planet gives them nothing to convert

**Status: Accepted.** Recorded for [#66](https://github.com/ajare/numina-space-industries/issues/66), which is the documentation ticket under the specification of [#59](https://github.com/ajare/numina-space-industries/issues/59); that issue is authoritative for the whole feature. Vocabulary is defined in [CONTEXT.md](../../CONTEXT.md). This ADR sits on top of [ADR-0008](0008-orbital-period-drives-hull-exposure.md) and reuses its sun vector, its Nadir-lock frame and its planet view factor unchanged: it decides what the **Solar array** does with the sky that evaluator produces. It changes no code — it records why the derived generation formula is shaped the way it is, and which tempting "fixes" were considered and declined.

A **Solar array**'s generation is computed from its **Blanket area**, its **Cell technology**'s conversion efficiency, **Packing factor** and **Array factor**, and the direct stellar irradiance the authored **Orbit** actually delivers:

```
P = A_blanket × packing × η_cell × f_array × G_direct × (1 − eclipse)
```

`f_array` is the blanket-level derate between a bare cell on a test bench and a built
blanket on orbit — coverglass reflection and absorption, interconnect resistance,
cell mismatch and bypass-diode loss. None of those is modelled individually (there
is no per-loss input for them); the one factor carries the whole gap, and it is
calibrated so blanket output matches flight hardware rather than the datasheet of
a bare cell. Without it the model ran ~23–54% above real arrays.

Three assumptions are load-bearing in that one line, and each is restated in the code beside the formula (`SolarState::capacityKW()` in `station-systems/src/StationSystemsImpl.h`):

1. **Perfect star-tracking.** `cos(incidence) = 1`, always. There is no cosine loss term because the array is assumed to point at the star continuously — not because cosine loss was forgotten.
2. **No degradation by default.** The **Wear ledger** is opt-in and absent by default, so a newly authored array does not fade.
3. **Fixed cell temperature.** No temperature coefficient is carried, because no panel temperature is modelled.

The first of those is the one this ADR exists to defend, because it is what makes the second half of the environment disappear.

## A star-pointing array sees no planet while it is generating

This is a geometric identity in this project's own frame, not a simplification. Take the Nadir-lock convention of [ADR-0008](0008-orbital-period-drives-hull-exposure.md): **+z** is zenith, **+x** along-track, **+y** cross-track, the planet lying straight down the −z axis. The evaluator's sun unit vector is

```
s = (−cos τ · sin φ,  sin τ,  cos τ · cos φ)
```

with **τ** the **Axial tilt** and **φ** the orbit phase, φ = 0 at **Local noon**. Perfect tracking means the array normal `n = s`. The planet view factor for a planar element is the same result the hull uses, `(Rp/r)² × max(0, −n·ẑ)`, and substituting gives

```
−n·ẑ = −s_z = −cos τ · cos φ
```

An array generates only while it is sunlit, and sunlit means `s_z > 0`, which is exactly `cos τ · cos φ > 0`. So across the entire sunlit half-orbit `−n·ẑ` is **negative**, the `max(0, ·)` clamps it, and the array's view of the planet is **zero**. The array turns to face the planet only after the star has gone below its horizon — that is, during **Eclipse**, when `G_direct` is zero and there is nothing to convert. There is no instant at which a tracking array both sees the planet and has starlight. The two terms are mutually exclusive by construction, so the planet cannot contribute to generation even if one wanted it to.

## Planetary infrared is below every bandgap, including the germanium subcell

The geometric argument already closes the door; the physical one says why nobody should want to open it. A planet in this model is a grey body in radiative balance, and at a representative **250 K** its emission peaks by Wien's law at

```
λ_peak = 2898 µm·K / 250 K ≈ 11.6 µm  →  E = 1.24 eV·µm / 11.6 µm ≈ 0.107 eV
```

A photon can only lift a carrier across a junction if its energy exceeds that junction's bandgap. Every gap in the catalogue is far above the planet's photons:

| Cell | Bandgap | Longest convertible wavelength |
|---|---|---|
| GaAs (top and middle junctions) | 1.42 eV | 0.87 µm |
| Germanium (bottom subcell — the lowest gap we ship) | 0.66 eV | 1.88 µm |
| Silicon | 1.12 eV | 1.11 µm |

The planet's peak sits at 11.6 µm, six times the longest wavelength even the germanium subcell can use. And the short-wavelength tail is not a loophole: at 1.88 µm a 250 K body is suppressed by `exp(−hc/λkT) = exp(−30.6) ≈ 5 × 10⁻¹⁴` relative to its peak. Photocurrent from planetary infrared is not "small" — it is exactly zero, and would remain so for any planet this model can author.

## The error being declined is large, which is why it is written down

The exclusion is **not** justified by the planet being dim. In a low Earth orbit at this project's own numbers, a downward-facing surface collects roughly **210 W/m²** of planetary infrared plus **359 W/m²** of reflected sunlight — about **42% of full sun** (ADR-0008). Fold that into the convertible term for a 60 m² **Blanket area** and the model is handed 34 kW of incident power that no cell can turn into electricity; the infrared half alone is 12.6 kW. Counted through the array's own efficiency chain the invented figure is about 8 kW, counted whole it is 34 kW, and either way it is **the same order as the ≈19 kW that same blanket actually makes from the star** — a phantom second array, bolted on underneath, powered by warmth.

So the reasoning is deliberately the opposite of the intuitive one. The planet is excluded not because its shine is negligible, but because it is unconvertible (infrared) and, under the tracking assumption, not even visible (albedo). Both halves matter, and a future contributor who "corrects" the exclusion by adding a planet-shine term would be inventing power, not recovering it.

**Reflected sunlight is excluded on the same geometric grounds, and returns with them.** Albedo is convertible in principle — it is starlight — but a star-pointing array has no view of the reflecting disc while it generates, so it contributes nothing here. The note at the exclusion site says so explicitly: if tracking is ever relaxed, albedo comes back and must be modelled, not waved through.

## Why the tracking assumption is cheap to hold

Under a single-axis drive about the orbit normal — the standard satellite configuration — the incidence of a rotating panel normal on this project's own sun vector reduces to `cos τ · cos(φ − θ)`. Driving the panel angle `θ` to the orbit phase `φ` cancels the phase term outright and leaves a constant `cos τ`. Perfect tracking therefore needs no per-tick trigonometry beyond what the exposure evaluator already performs, and collapses the whole environment coupling to **one scalar per tick**: the bridge pushes `G × (1 − eclipse)` in W/m² and every derived array reads it. Both wings see the same value, which is why one station-wide scalar is honest rather than lossy.

That is the trade this ADR makes: modelling fidelity traded for a single number on the wire. What is bought back is a formula with no incidence term, no per-array illumination, and no attitude model.

## Deliberate exclusions, and the reason for each

Each of these was considered and declined. They are recorded here so each reads as declined rather than overlooked.

**Radiation degradation and fluence.** There is no radiation environment in this simulation to apply a fluence against, so there is nothing for such a model to read. The **Wear ledger** — cycles, operating hours, throughput, calendar time — remains the only decline mechanism, and it is explicitly not a radiation model. The industry's radiation performance factor is additionally ambiguous in common usage and is deliberately not adopted. Adding radiation degradation requires building the environment first, under its own gate.

**Panel temperature.** A temperature coefficient is meaningless without a panel temperature, and no panel is a modelled thermal body. If panels ever become exterior surfaces in the native structural thermal model (the territory of [ADR-0007](0007-native-structural-thermal-contract.md) and [ADR-0008](0008-orbital-period-drives-hull-exposure.md)), the coefficient arrives together with the temperature that justifies it — never before.

**Self-shadowing.** One wing falling into the station's own shadow is a per-face visibility problem requiring ray-casting against Installation geometry every step. That would not survive the serial performance budget for fidelity that changes no decision an author makes. It is the same convexity concession the diffuse **Radiation cavity** already documents.

**Per-side panel modelling.** Two wings are optically identical under perfect tracking: same area total, same irradiance, same numbers. Modelling them separately would duplicate records that can never disagree. Splitting one array into two later is a data edit with a reason behind it, not a schema change — which is what makes this safe to defer.

**A tracking mechanism.** No drive axis, gimbal, articulation schedule or drive fault is modelled. Tracking is assumed perfect and free. Note that the existing `SolarTrackingFailureSettings` fault does not contradict this: it clamps the array to a fixed availability and is an availability abstraction, not a model of broken drive geometry. It never reintroduces an incidence term, so it never reopens the planet question.

## What would reopen this ADR

Any change that relaxes perfect tracking. The moment `n ≠ s`, the identity `−n·ẑ = −cos τ · cos φ ≤ 0` no longer holds, the array can see the planet while sunlit, and albedo becomes a real convertible input that must be modelled rather than excluded. Infrared still contributes nothing — the bandgap argument is not an assumption and cannot be relaxed — but the geometric half of the exclusion dies with the assumption that carried it.

A change to the wire format of the derived formula is hard to reverse for the same reason this decision is recorded: `areaM2` present is the discriminator between derived and legacy arrays, so the shape of the term is already committed to the document format. Reopening this ADR means reopening both.
