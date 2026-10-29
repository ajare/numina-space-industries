# Owner S0 fixture

`owner-station-s0.yaml` is a byte-for-byte capture of the owner's
`~/station.yaml`, obtained locally for #34 on 2026-10-29. SHA-256:
`41a69a1a8d14fe2b98fc308bc3f554ef084f936eda817ed715a1104acab1faf9`.
It is schema v4, 16 × 16 Cells, MultiLayerBlanket Hull, InsulatedPartition.
S0 is the 12-Cell Interior at x=13..15, y=6..9, UID 19. Its exterior Door
is the vertical Wall at (16,7). No saved temperatures or playback controls
are present. Keep the complete small document rather than invent neighbouring
boundary conditions in a cropped room. CI reads only this checked-in copy.

`nsi-structural-s0` opens that existing Door at Reset and runs 1 s. It asserts
pressure loss, separate gas/surface temperatures, portal escape and whole-system
energy closure each 0.1 s. It does not prescribe an exact curve or monotonic
gas cooling. A second, explicitly controlled-drain derivative starts from the
same closed document with S0's finite-inventory leak rate set to 1e9 kg/s;
this reaches exact evacuation in bounded CI time, then checks continued local
solid evolution with unchanged capacity and energy closure. This is not a claim
about the original scenario's natural time to vacuum. Existing component tests
cover trace/unresolved inventory and natural prolonged decompression separately.

Observed candidate diagnostic (not a golden assertion): after 1 s, S0 mean
pressure 101325 -> 21644.5 Pa, Door-adjacent gas/surface 284.313 / 292.83 K.
Physical defaults and natural blowdown trajectories remain subject to #35 review.
