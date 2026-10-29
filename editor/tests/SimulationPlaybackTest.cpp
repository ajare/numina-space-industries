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

void doorControls() {
    using namespace station::editor;
    using I=nsi::core::Installation;
    EditorSession session; session.newDocument(5,5);
    const auto wall=session.installation().level(0).horizontalWall(2,0);
    session.setHullType(std::nullopt);
    session.setDoor(0,wall,true);
    const auto authored=session.installation();
    const auto dirty=session.unsavedChanges();
    const auto canUndo=session.canUndo(), canRedo=session.canRedo();
    SimulationPlayback p;
    require(!p.toggleDoor(0,wall),"Door operated without runtime");
    p.reset(authored);
    require(!p.doorOpen(0,wall) && p.openDoors(0).empty(),"Door did not start closed");
    require(!p.toggleDoor(0,wall),"Door operated before Play");
    const auto initial=p.runtime()->snapshot();
    require(initial.atmospherePathways.size()==1 && initial.atmospherePathways[0].openFraction==0,
        "Door pathway not initially closed");
    p.togglePlaying();
    for (int n=0;n<10;++n) p.update(0.1);
    near(p.runtime()->snapshot().atmospheres[0].oxygenKg,initial.atmospheres[0].oxygenKg);
    const auto before=p.runtime()->snapshot();
    require(!p.toggleDoor(0,0) && !p.toggleDoor(1,wall),"Non-Door target operated");
    require(p.toggleDoor(0,wall) && p.doorOpen(0,wall),"Running Door failed to open");
    const auto open=p.runtime()->snapshot();
    require(open.atmospherePathways[0].openFraction==1,"Opening was not instantaneous");
    near(open.elapsedSeconds,before.elapsedSeconds);
    near(open.atmospheres[0].oxygenKg,before.atmospheres[0].oxygenKg);
    require(p.openDoors(0)==std::vector<std::size_t>{wall} && p.openDoors(1).empty(),"Open Door display state incorrect");
    p.synchronize(authored);
    require(p.doorOpen(0,wall),"Unchanged document reset Door");
    p.update(0.1);
    const auto vented=p.runtime()->snapshot();
    require(vented.healthy && vented.atmospheres[0].oxygenKg<before.atmospheres[0].oxygenKg,"Open Door did not vent gas");
    p.togglePlaying();
    require(!p.toggleDoor(0,wall) && p.doorOpen(0,wall),"Paused Door operated");
    p.togglePlaying();
    require(p.toggleDoor(0,wall) && !p.doorOpen(0,wall),"Door failed to close");
    require(p.runtime()->snapshot().atmospherePathways[0].openFraction==0,"Closing was not instantaneous");
    p.update(0.1);
    near(p.runtime()->snapshot().atmospheres[0].oxygenKg,vented.atmospheres[0].oxygenKg);
    p.toggleDoor(0,wall);
    p.reset(authored);
    require(!p.doorOpen(0,wall) && p.openDoors(0).empty(),"Reset retained open Door");
    require(session.installation()==authored && session.unsavedChanges()==dirty
        && session.canUndo()==canUndo && session.canRedo()==canRedo,"Runtime Door modified authoring/history");
    p.togglePlaying(); p.toggleDoor(0,wall); p.synchronize(nullptr);
    require(!p.doorOpen(0,wall) && p.openDoors(0).empty(),"Closed document retained Door state");
    // A Door whose two sides lead around into the same room still operates.
    I loop(5,5); const auto g=loop.level(0); const auto gap=g.horizontalWall(2,2);
    for (unsigned x:{1,3}) loop.toggleWall(0,g.horizontalWall(x,2));
    loop.setDoor(0,gap,true);
    p.reset(loop); p.togglePlaying();
    require(p.runtime()->snapshot().atmospherePathways.empty(),"Same-room Door created self-pathway");
    require(p.toggleDoor(0,gap) && p.doorOpen(0,gap),"Same-room Door would not operate");
    loop.setName("New document"); p.synchronize(loop);
    require(!p.doorOpen(0,gap),"Document replacement retained open Door");
    // An explicit interior Door exchanges gas only after opening.
    I pair(5,5); pair.setHullType(std::nullopt); const auto pg=pair.level(0);
    for (unsigned x=0;x<5;++x) pair.toggleWall(0,pg.horizontalWall(x,2));
    pair.setDoor(0,gap,true);
    auto yaml=pair.serialize(); const auto preset=yaml.find("atmosphere: Earth-like");
    const auto line=yaml.rfind('\n',preset)+1;
    yaml.insert(yaml.find('\n',preset),"\n"+std::string(preset-line,' ')+"atmosphereLoads: {leakRateKgPerS: 0.1}");
    pair.deserialize(yaml); p.reset(pair); p.togglePlaying();
    for (int n=0;n<10;++n) p.update(0.1);
    require(p.runtime()->snapshot().atmospherePathways[0].netTransferredKg[1]==0,"Closed Door mixed gas");
    p.toggleDoor(0,gap); p.update(0.1);
    const auto mixed=p.runtime()->snapshot();
    require(mixed.healthy && mixed.atmospherePathways[0].netTransferredKg[1]!=0,"Open interior Door did not exchange gas");
    p.toggleDoor(0,gap); p.update(0.1);
    near(p.runtime()->snapshot().atmospherePathways[0].netTransferredKg[1],mixed.atmospherePathways[0].netTransferredKg[1]);
}

void editingLockout() {
    using namespace station::editor;
    using I = nsi::core::Installation;
    EditorSession session;
    session.newDocument(2, 2);
    session.setName("Before");
    session.setName("After");
    session.undo(); // Keep both undo and redo history to exercise both paths.
    SimulationPlayback playback;
    playback.synchronize(session.installation());
    auto policy = [&] { session.setEditingEnabled(playback.editingAllowed()); };
    auto blocked = [&] {
        policy();
        require(!session.editingEnabled(), "Playback did not lock editing");
        require(!session.canUndo() && !session.canRedo(), "Locked history controls enabled");
        const auto before = session.installation();
        const auto dirty = session.unsavedChanges();
        session.setHullType(I::HullType::BareSkin);
        require(session.installation() == before, "Hull selector bypassed lock");
        session.setPartitionType(I::PartitionType::BarePartition);
        require(session.installation() == before, "Partition selector bypassed lock");
        session.setName("Blocked");
        session.setSector(0, 0, "Blocked", 2);
        session.setOffset(0, 1, 2);
        session.addLevel(3, 3);
        session.removeTopLevel();
        session.toggleCell(0, 0, 0);
        session.beginStroke(0, 0, 0);
        session.paintStroke(1, 0);
        session.endStroke();
        session.toggleWall(0, 0);
        session.beginWallStroke(0, 0);
        session.paintWallStroke(0);
        session.endStroke();
        session.undo();
        session.redo();
        require(session.installation() == before && session.unsavedChanges() == dirty,
                "Installation mutation bypassed playback lock");
        require(!session.strokeActive(), "Locked input started a stroke");
        playback.synchronize(session.installation());
    };
    policy();
    require(session.editingEnabled(), "Paused at start was locked");
    playback.togglePlaying();
    blocked(); // Playing at time zero is locked, even before the first update.
    require(playback.playing(), "Blocked edit reset playback");
    playback.togglePlaying();
    policy();
    require(session.editingEnabled(), "Pause at time zero stayed locked");
    playback.togglePlaying();
    playback.update(0.1);
    blocked();
    playback.togglePlaying();
    blocked(); // Pausing after advancing does not unlock authoring.
    require(playback.runtime()->snapshot().elapsedSeconds > 0, "Blocked edit reset time");
    playback.reset(session.installation());
    policy();
    require(session.editingEnabled() && session.canUndo() && session.canRedo(),
            "Reset did not restore editing and history");
    session.redo();
    require(session.installation().getName() == "After", "Lock damaged redo history");
    session.undo();
    require(session.installation().getName() == "Before", "Lock damaged undo history");
    session.setHullType(I::HullType::BareSkin);
    session.setPartitionType(I::PartitionType::BarePartition);
    require(session.installation().hullType() == I::HullType::BareSkin &&
            session.installation().partitionType() == I::PartitionType::BarePartition,
            "Reset did not unlock assembly selectors");
    const auto beforeStroke = session.installation();
    session.beginStroke(0, 0, 0);
    session.paintStroke(0, 0);
    session.setEditingEnabled(false);
    const auto afterStroke = session.installation();
    session.paintStroke(1, 0);
    require(!session.strokeActive() && session.installation() == afterStroke,
            "Lock did not finish in-flight stroke");
    session.setEditingEnabled(true);
    session.undo();
    require(session.installation() == beforeStroke, "Lock lost in-flight stroke history");
}

void temperatureHistory() {
    using I=nsi::core::Installation;
    using station::editor::SimulationPlayback;
    I installation(2,1);
    installation.toggleWall(0,installation.level(0).verticalWall(1,0));
    SimulationPlayback playback;
    playback.reset(installation);
    const auto initial=playback.runtime()->snapshot();
    for (const auto& air:initial.atmospheres) {
        const auto samples=playback.temperatureHistory(air.id);
        require(samples.size()==1,"History did not start at time zero");
        near(samples[0].seconds,0); near(samples[0].temperatureC,air.temperatureC);
    }
    require(playback.temperatureHistory("missing").empty(),"Unknown compartment gained history");
    for (int speed : {0,2,4}) {
        playback.reset(installation);
        playback.setSpeedIndex(speed);
        playback.togglePlaying();
        const int frames=static_cast<int>(70.0 / (0.1 * SimulationPlayback::speeds[speed])) + 1;
        for (int frame=0;frame<frames;++frame) playback.update(0.1);
        const auto snapshot=playback.runtime()->snapshot();
        // Every Sector is recorded without needing to select it in the UI.
        for (const auto& air:snapshot.atmospheres) {
            const auto samples=playback.temperatureHistory(air.id);
            require(samples.size()>50 && samples.size()<=602,"Minute history is not bounded");
            require(samples.front().seconds >= snapshot.elapsedSeconds-60.0-1e-8,
                "Expired sample remained in graph");
            require(samples.front().seconds <= snapshot.elapsedSeconds-58.9,
                "History did not retain the last minute");
            require(samples.back().seconds <= snapshot.elapsedSeconds &&
                samples.back().seconds > snapshot.elapsedSeconds-0.11,"History is not on simulation time");
            for (std::size_t n=1;n<samples.size();++n)
                require(samples[n].seconds>samples[n-1].seconds,"Duplicate or unordered history samples");
            require(samples.back().temperatureC < samples.front().temperatureC,"History did not capture cooling");
        }
        const auto id=snapshot.atmospheres[0].id;
        const auto count=playback.temperatureHistory(id).size();
        const auto last=playback.temperatureHistory(id).back();
        playback.togglePlaying(); playback.update(100);
        require(playback.temperatureHistory(id).size()==count,"Paused graph added samples");
        near(playback.temperatureHistory(id).back().seconds,last.seconds);
        near(playback.temperatureHistory(id).back().temperatureC,last.temperatureC);
    }
    const auto id=initial.atmospheres[0].id;
    playback.reset(installation);
    require(playback.temperatureHistory(id).size()==1,"Reset retained old temperature graph");
    near(playback.temperatureHistory(id)[0].temperatureC,20);
    installation.setName("Replacement");
    playback.synchronize(installation);
    require(playback.temperatureHistory(id).size()==1,"Document replacement retained graph");
    installation.setCell(0,0,0,I::SectorType::Void);
    playback.synchronize(installation);
    require(playback.temperatureHistory(id).empty(),"Removed compartment retained graph");
    playback.synchronize(nullptr);
    for (const auto& air:initial.atmospheres)
        require(playback.temperatureHistory(air.id).empty(),"Closed document retained graph");
}

void hullPlayback() {
    using I=nsi::core::Installation;
    auto temperature=[](I::HullType type) {
        station::editor::EditorSession session;
        session.newDocument(1,1);
        session.setHullType(type);
        station::editor::SimulationPlayback playback;
        playback.synchronize(session.installation());
        playback.setSpeedIndex(4);
        playback.togglePlaying();
        for (int frame=0;frame<600;++frame) playback.update(0.1);
        const auto state=playback.runtime()->snapshot();
        require(state.healthy && state.thermal.hull.has_value(),"Playback omitted Hull");
        require(state.thermal.hull->emittedEnergyKJ>0,"Playback Hull did not radiate");
        require(state.thermal.hull->absorbedEnergyKJ==0,"Playback sky not dark");
        return state.atmospheres[0].temperatureC;
    };
    const auto bare=temperature(I::HullType::BareSkin);
    const auto blanket=temperature(I::HullType::MultiLayerBlanket);
    require(bare<blanket-1.0 && blanket<20.0,"Playback did not wire Hull drain links");
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
        editingLockout();
        doorControls();
        documentAvailability();
        hullPlayback();
        temperatureHistory();
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
        // Geometry edits now require an explicit reset of the running preview.
        playback.reset(session.installation());
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
