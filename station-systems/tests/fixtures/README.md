# Legacy solar array fixtures

`legacy-solar-array.yaml` and `.bin` are the exact output of the pre-#60
`Persistence.cpp` at commit `8d226b1`, using the public YAML and binary backends.
The settings enable Station systems and contain one array (`id = sun`,
`capacityKW = 20`), with everything else defaulted. The enclosing map is named
`clientDocument` and uses the feature-selected schema, 137.

These are compatibility baselines, not snapshots to regenerate from the current
writer. The persistence consumer loads them, checks legacy defaults, and saves
them byte-for-byte.
