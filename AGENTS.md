# Development

Build and test in Release only. Use incremental builds where possible and run
CTest with `-j 16`. Public APIs and target names remain `station::`,
`StationSystems::StationSystems`, and `Promethium::Serialization`.

This repository owns Station Systems, generic serialization, public-library
scenarios/support and shared reference fixtures. Do not introduce dependencies
on Promethium, World, editor, graphics or application resource management.
