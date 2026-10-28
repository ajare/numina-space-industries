# Core

C++20 foundation for classes in `nsi::core`, using `Promethium::Serialization`.
No existing classes or APIs have been moved.

`Installation` stores a name (empty by default), exposed through `getName()`
and `setName(std::string)`. Include `<nsi/core/Installation.h>` or the umbrella
header `<nsi/core/Core.h>`.

`serialize()` returns a YAML map containing `name`; `deserialize(yaml)` loads
that map. The name field is required (empty strings are allowed). Invalid YAML
or a missing/non-convertible name throws `serialization::SerializationException`
without changing the Installation. String conversion follows the serialization
module (for example, YAML `null` becomes the string `"null"`). Unknown fields
are ignored.

Link the CMake target `nsi::core` (alias of `nsi-core`). Public headers live
under `include/nsi/core/` and are included as `<nsi/core/Core.h>`.
Implementation files live in `src/`.

The root build includes this module automatically. Consumers can also use
`add_subdirectory(path/to/core)` without the Editor or Station Systems.

## Standalone build and test

Builds reuse an installed yaml-cpp CMake package when available, otherwise
fetch the same pinned version used by Station Systems (requiring network
access on first configuration).

```sh
cmake -S core -B build-core -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release -j 16
ctest --test-dir build-core -C Release -j 16 --output-on-failure
```
