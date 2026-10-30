# Eclipse penumbra (#44)

`nsi::systems::StellarExposureEvaluator` is the sole orbital formula owner.
It smooths the geometric cylindrical shadow interval with a uniform window
of `0.533 / 360` orbital turns (the Sun's angular diameter). Each isolated
entry/exit is a linear ramp centred on the geometric crossing, with duration
`0.533 * orbitalPeriodS / 360`: **8.246694 s** for the default 5,570 s orbit
and **127.570589 s** for the 86,164 s GEO orbit.

`eclipse` is the blocked fraction in [0, 1]. `sunlit` means `eclipse < 1`,
including partial penumbra. Both direct stellar input and reflected sunlight
are multiplied by `1 - eclipse`; the boolean is not a reflected-light weight.
Planet infrared and the effective Deep-space sink are unaffected. Total
eclipse still has exactly zero direct and reflected input.

For grazing eclipses shorter than the ramp, the two ramps overlap: the peak
blocked fraction is geometric eclipse duration divided by ramp duration.
This avoids forcing totality and tends continuously to zero at critical tilt.
Above critical tilt there is no eclipse or penumbra. This is a timing model,
not a resolved solar-disc occultation or planetary illumination model.

The summary's Eclipse/Sunlit durations remain the geometric values, equivalently
the integrals of `eclipse` and `1 - eclipse` over an orbit. They sum to the
Orbital period, including grazing cases. They are **not** time spent in total
eclipse or with the boolean sunlit flag set. The summary labels explain the
fractional weighting. Phase offset shifts the profile without changing these
integrals.

The Editor publishes `1 - eclipse` unchanged at committed observation times.
Its existing sample-and-hold trace retains fractional penumbra values; shaded
bands indicate only sampled total eclipse (zero exposure), not penumbra. The
observation cadence can miss short ramps; it neither recomputes orbital timing
nor invents intermediate observations. No sky/override remains an unavailable
gap, not zero exposure.

The numerical station-systems interface, energy ledgers, time sampling and
rollback protocol are unchanged. Analytic evaluator tests pin durations, shape,
symmetry, period scaling and grazing continuity. Transport tests exercise
allocation-free successful penumbra steps, replay after refusal, atomic state
restoration and energy closure. Headless Editor tests cover fractional trace
publication and totality-only shading.
