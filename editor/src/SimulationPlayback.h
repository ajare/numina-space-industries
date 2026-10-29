#pragma once

#include <nsi/core/Installation.h>
#include <station/StationSystemsRuntime.h>

#include <array>
#include <memory>
#include <optional>

namespace station::editor {
// Runtime preview only: simulation never writes into the authored document.
class SimulationPlayback {
public:
    static constexpr std::array<double, 5> speeds{0.1, 0.5, 1.0, 2.0, 10.0};
    // nullptr means no active Installation: retire the preview and pause.
    void synchronize(const nsi::core::Installation* installation);
    void synchronize(const nsi::core::Installation& installation) { synchronize(&installation); }
    void reset(const nsi::core::Installation& installation);
    void togglePlaying() noexcept { if (runtime_) playing_ = !playing_; }
    bool playing() const noexcept { return playing_; }
    void setSpeedIndex(int index);
    int speedIndex() const noexcept { return speedIndex_; }
    void update(double wallSeconds);
    const StationSystemsRuntime* runtime() const noexcept { return runtime_.get(); }
    // Read-only snapshot only when the preview still matches this document.
    // Geometry edits may occur later in the frame than synchronization.
    std::optional<StationSystemsSnapshot> snapshotFor(const nsi::core::Installation& installation) const;
private:
    static constexpr double updateIntervalSeconds = 0.1;
    // Bound catch-up after stalls/native dialogs; discard excess wall-time debt.
    static constexpr int maxCatchUpUpdates = 5;
    std::optional<nsi::core::Installation> source_;
    std::unique_ptr<StationSystemsRuntime> runtime_;
    double remainder_ = 0;
    int speedIndex_ = 2;
    bool playing_ = false;
};
} // namespace station::editor
