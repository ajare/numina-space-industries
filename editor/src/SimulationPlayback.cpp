#include "SimulationPlayback.h"

#include <nsi/systems/Atmosphere.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace station::editor {
void SimulationPlayback::synchronize(const nsi::core::Installation* installation) {
    if (!installation) {
        playing_ = false;
        runtime_.reset();
        temperatureHistory_.clear();
        source_.reset();
        remainder_ = 0;
    } else if (!source_ || *source_ != *installation) reset(*installation);
}

void SimulationPlayback::reset(const nsi::core::Installation& installation) {
    playing_ = false;
    runtime_.reset();
    temperatureHistory_.clear();
    source_ = installation;
    remainder_ = 0;
    StationSystemsSettings settings;
    settings.enabled = true;
    settings.systemsStepWorldTicks = 1;
    settings.observationIntervalSeconds = 1;
    settings.atmospheres = nsi::systems::expandInstallation(installation);
    auto thermal = nsi::systems::expandThermal(installation);
    settings.hull = std::move(thermal.hull);
    settings.thermalLinks = std::move(thermal.links);
    settings.solarCycle = std::move(thermal.solarCycle);
    runtime_ = std::make_unique<StationSystemsRuntime>(std::move(settings), StationSystemsUpdateMode::Variable);
    recordTemperatures();
}

std::span<const TemperatureSample> SimulationPlayback::temperatureHistory(const std::string& id) const {
    const auto it = temperatureHistory_.find(id);
    return it == temperatureHistory_.end() ? std::span<const TemperatureSample>{} : it->second;
}

void SimulationPlayback::recordTemperatures() {
    const auto snapshot = runtime_->snapshot();
    for (const auto& air : snapshot.atmospheres) {
        auto& samples = temperatureHistory_[air.id];
        // Sample on simulation time, not frame time: slow playback must not
        // create an unbounded number of points within the one-minute window.
        if (samples.empty() || snapshot.elapsedSeconds - samples.back().seconds
                >= historySampleIntervalSeconds - 1e-9)
            samples.push_back({snapshot.elapsedSeconds, air.temperatureC});
        const auto first = std::lower_bound(samples.begin(), samples.end(),
            snapshot.elapsedSeconds - historySeconds,
            [](const auto& sample, double time) { return sample.seconds < time; });
        samples.erase(samples.begin(), first);
    }
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
        remainder_ = std::min(remainder_ + wallSeconds, maxCatchUpUpdates * updateIntervalSeconds);
        const int updates = static_cast<int>(std::floor((remainder_ + 1e-9) / updateIntervalSeconds));
        remainder_ = std::max(0.0, remainder_ - updates * updateIntervalSeconds);
        for (int i = 0; i < updates; ++i) {
            if (!runtime_->update(updateIntervalSeconds * speeds[speedIndex_])) {
                const auto diagnostics = runtime_->diagnostics();
                throw std::runtime_error(diagnostics.empty() ? "Simulation advance failed"
                                                            : diagnostics.front().message);
            }
            recordTemperatures();
        }
    } catch (...) {
        playing_ = false;
        throw;
    }
}
} // namespace station::editor
