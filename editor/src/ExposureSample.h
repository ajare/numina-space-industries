#pragma once

namespace station::editor {
// One committed Stellar exposure observation on the thermal graph: the
// fraction of full sun the Installation's sky is holding at that simulated
// instant, 1 in direct sunlight and 0 in Eclipse.
//
// NaN means there is no observation at that time - no authored Orbit, or a
// hand-authored solar profile that owns the whole stellar term instead of
// the sky. It renders as a gap. A zero would claim an eclipse that was
// never authored.
struct ExposureSample {
    double seconds{};
    double exposure{};
};
} // namespace station::editor
