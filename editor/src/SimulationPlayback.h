#pragma once

#include "TemperatureSample.h"
#include "PressureSample.h"
#include "SectorAtmosphere.h"
#include <nsi/core/Installation.h>
#include "CellPlaybackRuntime.h"

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
    // Runtime-only, instantaneous controls. Paused/missing/non-Door targets refuse.
    bool toggleDoor(std::size_t level, std::size_t wall);
    bool doorOpen(std::size_t level, std::size_t wall) const;
    std::vector<std::size_t> openDoors(std::size_t level) const;
    const CellPlaybackRuntime* runtime() const noexcept { return runtime_.get(); }
    // Read-only snapshot only when the preview still matches this document.
    // Geometry edits may occur later in the frame than synchronization.
    std::optional<StationSystemsSnapshot> snapshotFor(const nsi::core::Installation& installation) const;

    // Last minute of simulation time, collected for every compartment even
    // while its Sector is not selected. Invalidated by reset/update/synchronize.
    std::span<const TemperatureSample> temperatureHistory(const std::string& atmosphereId) const;
    std::span<const PressureSample> pressureHistory(const std::string& atmosphereId) const;
    std::span<const AtmosphereSample> atmosphereHistory(const std::string& atmosphereId) const;
private:
    void recordAtmosphereHistory();
    struct DoorState { bool open=false; };
    std::map<std::pair<std::size_t,std::size_t>, DoorState> doors_;
    std::map<std::string, std::vector<TemperatureSample>> temperatureHistory_;
    std::map<std::string, std::vector<PressureSample>> pressureHistory_;
    std::map<std::string, std::vector<AtmosphereSample>> atmosphereHistory_;
    static constexpr double historySeconds = 60.0;
    static constexpr double historySampleIntervalSeconds = 0.1;
    static constexpr double updateIntervalSeconds = 0.1;
    // Bound work after stalls/native dialogs; retain unadvanced wall-time debt.
    static constexpr int maxCatchUpUpdates = 5;
    std::optional<nsi::core::Installation> source_;
    std::unique_ptr<CellPlaybackRuntime> runtime_;
    std::optional<double> pendingStepSeconds_;
    double remainder_ = 0;
    int speedIndex_ = 2;
    bool playing_ = false;
};
} // namespace station::editor
