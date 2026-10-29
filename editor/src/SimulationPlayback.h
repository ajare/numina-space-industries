#pragma once

#include "TemperatureSample.h"
#include <nsi/core/Installation.h>
#include <station/StationSystemsRuntime.h>

#include <array>
#include <memory>
#include <optional>
#include <map>
#include <span>
#include <string>
#include <vector>

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
    bool editingAllowed() const {
        return !playing_ && (!runtime_ || runtime_->snapshot().elapsedSeconds == 0.0);
    }
    void setSpeedIndex(int index);
    int speedIndex() const noexcept { return speedIndex_; }
    void update(double wallSeconds);
    const StationSystemsRuntime* runtime() const noexcept { return runtime_.get(); }
    // Last minute of simulation time, collected for every compartment even
    // while its Sector is not selected. Invalidated by reset/update/synchronize.
    std::span<const TemperatureSample> temperatureHistory(const std::string& atmosphereId) const;
private:
    void recordTemperatures();
    std::map<std::string, std::vector<TemperatureSample>> temperatureHistory_;
    static constexpr double historySeconds = 60.0;
    static constexpr double historySampleIntervalSeconds = 0.1;
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
