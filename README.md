# Numina Space Industries

Source-consumed C++20 Station simulation and generic serialization, clean-imported
from Promethium's `refactor/library` worktree. APIs and simulation behavior are
unchanged. See [Station Systems](station-systems/README.md) for API contracts.

## Build and test

Requires CMake 3.24+, a C/C++20 toolchain and Git. CMake uses installed yaml-cpp
when available, otherwise fetches its pinned source; Lua is fetched at a pinned
revision. First configuration therefore requires network access unless those
dependencies are cached.

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --config Release -j 16
ctest --test-dir build-linux -C Release -j 16 --output-on-failure
```

Consumers may use `add_subdirectory(path/to/numina-space-industries)` or add
`station-systems/` directly. Link `StationSystems::StationSystems`; generic
serialization consumers link `Promethium::Serialization`.

`resources/` contains shared reference/test inputs, not a required runtime asset
installation. The library embeds its own default Lua. World/editor-specific
code, documents and assets remain in Promethium.
