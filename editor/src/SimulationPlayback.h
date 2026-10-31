#pragma once

#include "TemperatureSample.h"
#include "StructuralView.h"
#include "PressureSample.h"
#include "ExposureSample.h"
#include "SolarGenerationSample.h"
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
#include <utility>

namespace station::editor {
// Runtime preview only: simulation never writes into the authored document.
class SimulationPlayback {
public:
    explicit SimulationPlayback(StationSystemsSource lifecycleSource=std::nullopt)
        :lifecycleSource_(std::move(lifecycleSource)) {}
    static constexpr std::array<double, 5> speeds{0.1, 0.5, 1.0, 2.0, 10.0};
    // nullptr means no active Installation: retire the preview and pause.
    void synchronize(const nsi::core::Installation* installation);
    void synchronize(const nsi::core::Installation& installation) { synchronize(&installation); }
    void reset(const nsi::core::Installation& installation);
    void togglePlaying() noexcept { if (runtime_) playing_ = !playing_; }
    bool playing() const noexcept { return playing_; }
    bool editingAllowed() const {
        return !playing_ && !injected_ && (!runtime_ || runtime_->snapshot().elapsedSeconds == 0.0);
    }
    void setSpeedIndex(int index);
    int speedIndex() const noexcept { return speedIndex_; }
    // Supply the grid's current owned left-button hold each frame. Injection
    // uses simulated seconds while playing, wall seconds while paused (no flow).
    void update(double wallSeconds,std::optional<GasInjector> injection=std::nullopt);
    void setObservationOptions(CellObservationOptions options);
    CellObservationOptions observationOptions() const {return observationOptions_;}
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
    std::span<const StructuralSample> structuralHistory(std::size_t cavity) const;
    std::span<const TemperatureSample> temperatureHistory(const std::string& atmosphereId) const;
    std::span<const PressureSample> pressureHistory(const std::string& atmosphereId) const;
    std::span<const AtmosphereSample> atmosphereHistory(const std::string& atmosphereId) const;
    // The committed Stellar exposure trace, one sample per committed
    // observation and shared by every Sector because the sky is one thing.
    // NaN where no orbital sky is available, which renders as a gap.
    std::span<const ExposureSample> exposureHistory() const noexcept { return exposureHistory_; }
    // The committed Solar generation trace, one sample per committed
    // observation. Like the exposure trace it is one thing rather than a
    // per-Sector copy: a Station has one Solar array record. Samples are NaN
    // where the header would refuse a figure - no array, no authored Orbit,
    // or the array switched off - which renders as a gap.
    std::span<const SolarGenerationSample> solarHistory() const noexcept { return solarHistory_; }
private:
    void recordAtmosphereHistory(bool instantaneous=false);
    struct DoorState { bool open=false; };
    std::map<std::pair<std::size_t,std::size_t>, DoorState> doors_;
    std::map<std::size_t, std::vector<StructuralSample>> structuralHistory_;
    std::map<std::string, std::vector<TemperatureSample>> temperatureHistory_;
    std::map<std::string, std::vector<PressureSample>> pressureHistory_;
    std::map<std::string, std::vector<AtmosphereSample>> atmosphereHistory_;
    std::vector<ExposureSample> exposureHistory_;
    std::vector<SolarGenerationSample> solarHistory_;
    static constexpr double historySeconds = 60.0;
    static constexpr double historySampleIntervalSeconds = 0.1;
    static constexpr double updateIntervalSeconds = 0.1;
    // Bound work after stalls/native dialogs; retain unadvanced wall-time debt.
    static constexpr int maxCatchUpUpdates = 5;
    std::optional<nsi::core::Installation> source_;
    std::unique_ptr<CellPlaybackRuntime> runtime_;
    std::optional<double> pendingStepSeconds_;
    std::optional<GasInjector> pendingInjection_; // frozen with a refused duration
    double remainder_ = 0;
    int speedIndex_ = 2;
    bool playing_ = false;
    bool injected_ = false; // source-only edits at t=0 still require Reset
    CellObservationOptions observationOptions_{};
    const StationSystemsSource lifecycleSource_;
};
} // namespace station::editor
