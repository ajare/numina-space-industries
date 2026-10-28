# Core

Dependency-free C++20 foundation for new classes in `nsi::core`. This is an
empty, header-only scaffold; no existing classes or APIs have been moved.

Link the CMake target `nsi::core` (alias of `nsi-core`). Public headers live
under `include/nsi/core/` and are included as `<nsi/core/Core.h>`. Future
implementation files belong in `src/`; add compiled sources to the target
when needed.

The root build includes this module automatically. Consumers can also use
`add_subdirectory(path/to/core)` without the Editor or Station Systems.

## Standalone build and test

```sh
cmake -S core -B build-core -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release -j 16
ctest --test-dir build-core -C Release -j 16 --output-on-failure
```
