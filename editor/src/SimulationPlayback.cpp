#include "SimulationPlayback.h"

#include <nsi/systems/Atmosphere.h>
#include <cmath>
#include <stdexcept>

namespace station::editor {
void SimulationPlayback::synchronize(const nsi::core::Installation& installation) {
    if (!source_ || *source_ != installation) reset(installation);
}

void SimulationPlayback::reset(const nsi::core::Installation& installation) {
    playing_ = false;
    runtime_.reset();
    source_ = installation;
    targetSeconds_ = remainder_ = 0;
    StationSystemsSettings settings;
    settings.enabled = true;
    settings.systemsStepWorldTicks = 1;
    settings.observationIntervalSeconds = 1;
    settings.atmospheres = nsi::systems::expandInstallation(installation);
    runtime_ = std::make_unique<StationSystemsRuntime>(std::move(settings), stepSeconds);
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
        remainder_ += wallSeconds * speeds[speedIndex_];
        const auto steps = std::floor((remainder_ + 1e-9) / stepSeconds);
        if (steps < 1) return;
        remainder_ = std::max(0.0, remainder_ - steps * stepSeconds);
        targetSeconds_ += steps * stepSeconds;
        if (!runtime_->advanceTo(targetSeconds_)) {
            const auto diagnostics = runtime_->diagnostics();
            throw std::runtime_error(diagnostics.empty() ? "Simulation advance failed"
                                                        : diagnostics.front().message);
        }
    } catch (...) {
        playing_ = false;
        throw;
    }
}
} // namespace station::editor
