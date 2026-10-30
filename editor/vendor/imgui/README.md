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
(no test-engine hooks, no local patches) — the claim is about Dear ImGui
itself; the date picker sharing this directory is patched, and that is
recorded below.

Included files:

- Core: `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`
- Headers: `imgui.h`, `imgui_internal.h`, `imconfig.h`, `imstb_*.h`
- Backends: `backends/imgui_impl_sdl2.{h,cpp}`,
  `backends/imgui_impl_opengl3.{h,cpp}`,
  `backends/imgui_impl_opengl3_loader.h` (bundled GL loader; no GLEW/GLAD
  dependency)
- `LICENSE.txt` (MIT) — must stay with these sources

`imgui_demo.cpp` is intentionally excluded.

## Vendored ImGuiDatePicker

Snapshot of [ImGuiDatePicker](https://github.com/DnA-IntRicate/ImGuiDatePicker)
at commit `ca5e3a20f945c221e76e6bde3b09101acf589b5c` ("Fixed issue #3",
2025-02-05), by Adam Foflonker. MIT licensed; the notice is kept in
`LICENSE-ImGuiDatePicker.txt` and must stay with these sources.

- `ImGuiDatePicker.hpp`, `ImGuiDatePicker.cpp` — the calendar date-picker
  widget, used for the Editor's authored mission start date

### Local patch: `gmtime_s` is MSVC-only

Upstream's `Today()` calls `gmtime_s`, which is the MSVC "secure" variant
and **does not exist in glibc** — POSIX spells the same function
`gmtime_r`, with the arguments the other way round. As shipped the file
did not compile on Linux at all. It is now routed through a small
`BreakUtc()` helper guarded on `_MSC_VER`, which calls whichever the
platform actually has. Same behaviour on both; nothing else was touched.

### Configuration

`IMGUI_DATEPICKER_YEAR_MIN` / `IMGUI_DATEPICKER_YEAR_MAX` are set by
CMake on the `editor-imgui` target to `1970` / `2200`, matching
`nsi::core::kMinAuthoredYear` / `kMaxAuthoredYear`. A `static_assert` in
`editor/src/StartDatePicker.cpp` fails the build if the two ever drift,
so the picker cannot offer a date the document would refuse.
