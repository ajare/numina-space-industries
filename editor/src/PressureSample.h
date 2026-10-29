#pragma once
#include <optional>

namespace station::editor {
struct PressureSample {
    double seconds;
    double pressureKPa;
    std::optional<double> minimumKPa{},maximumKPa{};
};
} // namespace station::editor
