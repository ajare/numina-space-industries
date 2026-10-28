#include "SimulationPlayback.h"
#include "station/editor/EditorSession.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void near(double a, double b) {
    require(std::abs(a - b) < 1e-6, "Unexpected simulation time");
}
}

int main() {
    try {
        using namespace station::editor;
        EditorSession session;
        // A real load makes it impossible for a clock-only mock to pass.
        auto yaml = nsi::core::Installation(1, 1).serialize();
        const auto preset = yaml.find("atmosphere: Earth-like");
        require(preset != std::string::npos, "Missing atmosphere preset");
        const auto line = yaml.rfind('\n', preset) + 1;
        yaml.insert(yaml.find('\n', preset), "\n" + std::string(preset - line, ' ') +
                    "atmosphereLoads: {occupants: 1, oxygenUsePerOccupantKgPerS: 0.00001}");
        nsi::core::Installation installation;
        installation.deserialize(yaml);
        session.replaceDocument(installation);
        SimulationPlayback playback;
        playback.synchronize(session.installation());
        require(!playback.playing() && playback.speedIndex() == 2, "Wrong playback defaults");
        const double oxygen = playback.runtime()->snapshot().atmospheres.at(0).oxygenKg;
        playback.update(10);
        near(playback.runtime()->snapshot().elapsedSeconds, 0);
        for (int i = 0; i < 5; ++i) {
            playback.reset(session.installation());
            playback.setSpeedIndex(i);
            playback.togglePlaying();
            for (int frame = 0; frame < 80; ++frame) playback.update(0.125);
            near(playback.runtime()->snapshot().elapsedSeconds, 10 * SimulationPlayback::speeds[i]);
            require(playback.runtime()->snapshot().atmospheres.at(0).oxygenKg < oxygen,
                    "Simulation did not consume oxygen");
            playback.togglePlaying();
            const double elapsed = playback.runtime()->snapshot().elapsedSeconds;
            playback.update(100);
            near(playback.runtime()->snapshot().elapsedSeconds, elapsed);
            playback.togglePlaying();
            playback.update(1.25);
            require(playback.runtime()->snapshot().elapsedSeconds > elapsed, "Resume failed");
        }
        playback.reset(session.installation());
        require(!playback.playing(), "Reset did not pause");
        near(playback.runtime()->snapshot().elapsedSeconds, 0);
        near(playback.runtime()->snapshot().atmospheres.at(0).oxygenKg, oxygen);
        require(session.installation() == installation, "Preview modified authored document");
        playback.setSpeedIndex(2);
        playback.togglePlaying();
        playback.update(0.0625);
        near(playback.runtime()->snapshot().elapsedSeconds, 0);
        playback.togglePlaying();
        playback.update(20);
        playback.togglePlaying();
        playback.update(0.0625);
        near(playback.runtime()->snapshot().elapsedSeconds, 0.125);
        playback.synchronize(session.installation());
        require(playback.playing(), "Unchanged document reset playback");
        session.toggleCell(0, 0, 0);
        playback.synchronize(session.installation());
        require(!playback.playing(), "Geometry edit did not pause");
        near(playback.runtime()->snapshot().elapsedSeconds, 0);
        require(playback.runtime()->snapshot().atmospheres.empty(), "Old compartments survived edit");
        session.undo();
        playback.synchronize(session.installation());
        require(playback.runtime()->snapshot().atmospheres.size() == 1, "Undo did not rebuild preview");
        std::cout << "Simulation playback passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
