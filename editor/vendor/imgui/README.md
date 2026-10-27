# Vendored Dear ImGui (docking branch)

Snapshot of [Dear ImGui](https://github.com/ocornut/imgui) **1.89.5**, taken
from the `docking` branch at commit `0ea3b87bd63ecbf359585b7c235839146e84dedb`
("Merge branch 'master' into docking" — the docking-branch tree whose
`IMGUI_VERSION` is 1.89.5; note that the master-side bump commit `1ebb91382`
is *not* a docking tree).

Vendored deliberately (rather than FetchContent) so the Editor stays on the
exact docking-branch version proven by the sibling
`promethium-fermide-systems-simulation` application and builds without
network access. This snapshot is the unmodified upstream tree at that commit
(no test-engine hooks, no local patches).

Included files:

- Core: `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`
- Headers: `imgui.h`, `imgui_internal.h`, `imconfig.h`, `imstb_*.h`
- Backends: `backends/imgui_impl_sdl2.{h,cpp}`,
  `backends/imgui_impl_opengl3.{h,cpp}`,
  `backends/imgui_impl_opengl3_loader.h` (bundled GL loader; no GLEW/GLAD
  dependency)
- `LICENSE.txt` (MIT) — must stay with these sources

`imgui_demo.cpp` is intentionally excluded.
