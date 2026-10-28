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

void documentAvailability() {
    using namespace station::editor;
    struct Dialogs : FileDialogs {
        FileSelection next{FileOutcome::Cancelled, {}, {}};
        FileSelection openYaml() override { return next; }
        FileSelection saveYaml(const std::filesystem::path&) override { return next; }
    } dialogs;
    struct Files : DocumentFiles {
        bool fail = false;
        Installation read(const std::filesystem::path&) override {
            if (fail) throw std::runtime_error("Read failed");
            return Installation(1, 1);
        }
        void write(const std::filesystem::path&, const Installation&) override {
            if (fail) throw std::runtime_error("Write failed");
        }
    } files;
    EditorSession session;
    SimulationPlayback playback;
    auto synchronize = [&] {
        playback.synchronize(session.hasDocument() ? &session.installation() : nullptr);
    };
    require(!session.hasDocument(), "Startup placeholder counted as a loaded Installation");
    synchronize();
    playback.togglePlaying();
    playback.update(0.1);
    require(!playback.playing() && !playback.runtime(), "No-document playback was enabled");
    require(session.open(dialogs, files).outcome == FileOutcome::Cancelled && !session.hasDocument(),
            "Cancelled Open enabled playback");
    dialogs.next = {FileOutcome::Success, "installation.yaml", {}};
    files.fail = true;
    require(session.open(dialogs, files).outcome == FileOutcome::Failed && !session.hasDocument(),
            "Failed Open enabled playback");
    require(session.save(dialogs, files).outcome == FileOutcome::Failed && !session.hasDocument(),
            "Failed Save activated placeholder");
    files.fail = false;
    require(session.open(dialogs, files).outcome == FileOutcome::Success && session.hasDocument(),
            "Successful Open did not enable playback");
    synchronize();
    playback.togglePlaying();
    playback.update(0.1);
    require(playback.playing() && playback.runtime()->snapshot().worldTick == 1, "Loaded Installation did not run");
    playback.synchronize(nullptr);
    playback.togglePlaying();
    playback.update(0.1);
    require(!playback.playing() && !playback.runtime(), "Removing Installation retained a running preview");
    EditorSession created;
    created.newDocument(1, 1);
    require(created.hasDocument() && created.path().empty(), "New unsaved Installation was not active");
    playback.synchronize(created.hasDocument() ? &created.installation() : nullptr);
    playback.togglePlaying();
    playback.update(0.1);
    require(playback.runtime()->snapshot().worldTick == 1, "New unsaved Installation could not run");
    EditorSession saved;
    require(saved.save(dialogs, files).outcome == FileOutcome::Success && saved.hasDocument(),
            "Successful Save did not establish an Installation");
}
}

int main() {
    try {
        using namespace station::editor;
        documentAvailability();
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
            for (int frame = 0; frame < 200; ++frame) playback.update(0.05);
            require(playback.runtime()->snapshot().worldTick == 100, "Speed changed real-time update count");
            near(playback.runtime()->snapshot().systemsTimestepSeconds, 0.1 * SimulationPlayback::speeds[i]);
            near(playback.runtime()->snapshot().elapsedSeconds, 10 * SimulationPlayback::speeds[i]);
            require(playback.runtime()->snapshot().atmospheres.at(0).oxygenKg < oxygen,
                    "Simulation did not consume oxygen");
            near(playback.runtime()->snapshot().atmospheres.at(0).oxygenKg,
                 oxygen - 0.00001 * playback.runtime()->snapshot().elapsedSeconds);
            playback.togglePlaying();
            const double elapsed = playback.runtime()->snapshot().elapsedSeconds;
            playback.update(100);
            near(playback.runtime()->snapshot().elapsedSeconds, elapsed);
            playback.togglePlaying();
            playback.update(0.2);
            require(playback.runtime()->snapshot().elapsedSeconds > elapsed, "Resume failed");
        }
        playback.reset(session.installation());
        require(!playback.playing(), "Reset did not pause");
        near(playback.runtime()->snapshot().elapsedSeconds, 0);
        near(playback.runtime()->snapshot().atmospheres.at(0).oxygenKg, oxygen);
        require(session.installation() == installation, "Preview modified authored document");
        playback.setSpeedIndex(2);
        playback.togglePlaying();
        playback.update(0.05);
        near(playback.runtime()->snapshot().elapsedSeconds, 0);
        playback.togglePlaying();
        playback.update(20);
        playback.togglePlaying();
        playback.update(0.05);
        near(playback.runtime()->snapshot().elapsedSeconds, 0.1);
        playback.setSpeedIndex(4);
        playback.update(0.1);
        near(playback.runtime()->snapshot().elapsedSeconds, 1.1);
        require(playback.runtime()->snapshot().worldTick == 2, "Speed switch reset or subdivided preview");
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
        playback.setSpeedIndex(2);
        playback.togglePlaying();
        playback.update(100); // a dialog/stall must not cause an unbounded catch-up loop
        require(playback.runtime()->snapshot().worldTick == 5, "Catch-up work was not bounded");
        near(playback.runtime()->snapshot().elapsedSeconds, 0.5);
        std::cout << "Simulation playback passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
