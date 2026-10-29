#include "SimulationPlayback.h"

#include <nsi/systems/Atmosphere.h>
#include <cmath>
#include <stdexcept>

namespace station::editor {
void SimulationPlayback::synchronize(const nsi::core::Installation* installation) {
    if (!installation) {
        playing_ = false;
        runtime_.reset();
        source_.reset();
        remainder_ = 0;
    } else if (!source_ || *source_ != *installation) reset(*installation);
}

void SimulationPlayback::reset(const nsi::core::Installation& installation) {
    playing_ = false;
    runtime_.reset();
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
        }
    } catch (...) {
        playing_ = false;
        throw;
    }
}
} // namespace station::editor
