#include "SimulationPlayback.h"

#include <nsi/systems/Atmosphere.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace station::editor {
void SimulationPlayback::synchronize(const nsi::core::Installation* installation) {
    if (!installation) {
        playing_ = false;
        runtime_.reset();
        doors_.clear();
        temperatureHistory_.clear();
        pressureHistory_.clear();
        atmosphereHistory_.clear();
        source_.reset();
        remainder_ = 0;pendingStepSeconds_.reset();
    } else if (!source_ || *source_ != *installation) reset(*installation);
}

void SimulationPlayback::reset(const nsi::core::Installation& installation) {
    playing_ = false;
    runtime_.reset();
    doors_.clear();
    temperatureHistory_.clear();
    pressureHistory_.clear();
    atmosphereHistory_.clear();
    source_ = installation;
    remainder_ = 0;pendingStepSeconds_.reset();
    runtime_=std::make_unique<CellPlaybackRuntime>(installation,observationOptions_);
    for (const auto& door:runtime_->model().geometry().doors)
        doors_.emplace(std::pair{door.level,door.wall},DoorState{});
    recordAtmosphereHistory();
}

bool SimulationPlayback::toggleDoor(std::size_t level,std::size_t wall) {
    if (!playing_ || !runtime_) return false;
    const auto it=doors_.find({level,wall});
    if (it==doors_.end()) return false;
    auto& door=it->second;
    const bool open=!door.open;
    if (!runtime_->setDoorOpen(level,wall,open)) throw std::runtime_error("Native Door control failed");
    door.open=open;
    return true;
}
bool SimulationPlayback::doorOpen(std::size_t level,std::size_t wall) const {
    const auto it=doors_.find({level,wall});
    return it!=doors_.end() && it->second.open;
}
std::vector<std::size_t> SimulationPlayback::openDoors(std::size_t level) const {
    std::vector<std::size_t> result;
    for (const auto& [key,door]:doors_)
        if (key.first==level && door.open) result.push_back(key.second);
    return result;
}

std::span<const TemperatureSample> SimulationPlayback::temperatureHistory(const std::string& id) const {
    const auto it = temperatureHistory_.find(id);
    return it == temperatureHistory_.end() ? std::span<const TemperatureSample>{} : it->second;
}

std::span<const PressureSample> SimulationPlayback::pressureHistory(const std::string& id) const {
    const auto it=pressureHistory_.find(id);
    return it==pressureHistory_.end() ? std::span<const PressureSample>{} : it->second;
}

std::span<const AtmosphereSample> SimulationPlayback::atmosphereHistory(const std::string& id) const {
    const auto it=atmosphereHistory_.find(id);
    return it==atmosphereHistory_.end() ? std::span<const AtmosphereSample>{} : it->second;
}

void SimulationPlayback::recordAtmosphereHistory() {
    const auto& snapshot = runtime_->snapshot();
    auto record=[&](auto& samples, auto sample) {
        // Sample on simulation time, not frame time: slow playback must not
        // create an unbounded number of points within the one-minute window.
        if (samples.empty() || snapshot.elapsedSeconds - samples.back().seconds
                >= historySampleIntervalSeconds - 1e-9)
            samples.push_back(sample);
        const auto first = std::lower_bound(samples.begin(), samples.end(),
            snapshot.elapsedSeconds - historySeconds,
            [](const auto& sample, double time) { return sample.seconds < time; });
        samples.erase(samples.begin(), first);
    };
    for (const auto& air : snapshot.atmospheres) {
        record(temperatureHistory_[air.id],TemperatureSample{snapshot.elapsedSeconds,
            air.spatial && !air.resolvedGas ? std::numeric_limits<double>::quiet_NaN() : air.temperatureC});
        record(pressureHistory_[air.id],PressureSample{snapshot.elapsedSeconds,air.pressureKPa,
            air.spatial ? std::optional<double>(air.minCellPressureKPa) : std::nullopt,
            air.spatial ? std::optional<double>(air.maxCellPressureKPa) : std::nullopt});
        record(atmosphereHistory_[air.id],atmosphereSample(snapshot.elapsedSeconds,air));
    }
}

std::optional<StationSystemsSnapshot> SimulationPlayback::snapshotFor(
    const nsi::core::Installation& installation) const {
    if (!runtime_ || !source_ || *source_ != installation) return std::nullopt;
    return runtime_->snapshot();
}

void SimulationPlayback::setObservationOptions(CellObservationOptions options) {
    if (runtime_) runtime_->setObservationOptions(options);
    observationOptions_=options;
}
void SimulationPlayback::setSpeedIndex(int index) {
    if (index < 0 || index >= static_cast<int>(speeds.size()))
        throw std::out_of_range("Invalid simulation speed");
    speedIndex_ = index;
}

void SimulationPlayback::update(double wallSeconds) {
    if (!std::isfinite(wallSeconds) || wallSeconds < 0)
        throw std::invalid_argument("Invalid simulation frame duration");
    if (!playing_ || !runtime_) return;
    try {
        const double debt=remainder_+wallSeconds;
        if (!std::isfinite(debt)) throw std::invalid_argument("Simulation time debt exceeds finite range");
        remainder_=debt;
        for (int i=0;i<maxCatchUpUpdates && remainder_+1e-9>=updateIntervalSeconds;++i) {
            if (!pendingStepSeconds_) pendingStepSeconds_=updateIntervalSeconds*speeds[speedIndex_];
            if (!runtime_->update(*pendingStepSeconds_)) {
                const auto diagnostics = runtime_->diagnostics();
                throw std::runtime_error(diagnostics.empty() ? "Simulation advance failed"
                                                            : diagnostics.front().message);
            }
            remainder_=std::max(0.0,remainder_-updateIntervalSeconds);pendingStepSeconds_.reset();
            recordAtmosphereHistory();
        }
    } catch (...) {
        playing_ = false;
        throw;
    }
}
} // namespace station::editor
