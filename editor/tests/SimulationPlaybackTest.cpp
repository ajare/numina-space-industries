#include "SimulationPlayback.h"
#include "station/editor/EditorSession.h"

#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void near(double a, double b) {
    require(std::abs(a - b) < 1e-6, "Unexpected simulation time");
}

void lifecycleTickIntegration() {
    using namespace station::editor;using I=nsi::core::Installation;
    I scene(3,3);scene.setHullType(std::nullopt);scene.toggleWall(0,scene.level(0).horizontalWall(1,0));
    const std::string spy=R"lua(return function(name,...)
        local n,elapsed=0,0
        return {advance=function(self,tick,dt,start)
            assert(tick==n+1,'Lua advanced twice for one host tick')
            assert(math.abs(start-elapsed)<1e-10,'Lua clock differs from committed host clock')
            n=n+1;elapsed=elapsed+dt;return n
        end}
    end)lua";
    CellPlaybackRuntime runtime(scene,{},spy);const auto before=runtime.snapshot();
    require(!runtime.update(0.1,0),"Native refusal fixture did not fail");
    require(runtime.snapshot().completedTicks==std::array<uint64_t,4>{}
        && runtime.snapshot().cellAtmospheres==before.cellAtmospheres,"Prepared Lua published before native commit");
    require(runtime.update(0.1),"Same-duration retry reran a prepared Lua step");
    require(runtime.snapshot().completedTicks==std::array<uint64_t,4>{1,1,1,1},"Lifecycle not committed at host cadence");
    require(!runtime.update(0.2,0) && runtime.update(0.01),"Changed-duration retry did not restore Lua");
    near(runtime.snapshot().elapsedSeconds,0.11);
    require(runtime.snapshot().completedTicks==std::array<uint64_t,4>{2,2,2,2},"Lua ran per source/acoustic slice instead of host tick");
    CellPlaybackRuntime bundled(scene),observer(scene,{},spy);
    for (double dt:{0.1,0.01,0.5}) {
        require(bundled.update(dt) && observer.update(dt),"Mixed-duration lifecycle failed");
        for (std::size_t i=0;i<bundled.model().fluid().states().size();++i)
            require(bundled.model().fluid().states()[i].q==observer.model().fluid().states()[i].q,"Lua mirror replaced or double-processed authoritative gas");
    }
    const std::string failure=R"lua(return function(name)
        local n=0
        return {advance=function(self,tick)
            assert(tick==n+1,'earlier sandbox was not recovered');n=n+1
            if name=='ECLSS' and tick==2 then error('intentional lifecycle rejection') end
            return n
        end}
    end)lua";
    SimulationPlayback playback(failure);playback.reset(scene);playback.togglePlaying();playback.update(0.1);
    const auto committed=playback.runtime()->snapshot();const auto id=committed.atmospheres[0].id;
    const auto samples=playback.temperatureHistory(id).size();
    for (int attempt=0;attempt<2;++attempt) {
        bool refused=false;try {playback.update(attempt ? 0 : 0.1);} catch (const std::runtime_error&) {refused=true;}
        require(refused && !playback.playing(),"Lifecycle failure did not pause playback");
        require(playback.runtime()->snapshot().cellAtmospheres==committed.cellAtmospheres
            && playback.runtime()->snapshot().completedTicks==committed.completedTicks,"Lua failure advanced physical/script state");
        require(playback.runtime()->diagnostics()[0].system=="ECLSS" && playback.temperatureHistory(id).size()==samples,"Lua diagnostic/history wrong");
        if (attempt==0) playback.togglePlaying();
    }
    playback.reset(scene);require(playback.runtime()->snapshot().completedTicks==std::array<uint64_t,4>{},"Reset retained Lua state");
}
void rollingStepTiming() {
    using namespace station::editor;
    SimulationStepTiming timing;require(timing.sampleCount()==0,"Fresh timing window has samples");near(timing.averageMicroseconds(),0);
    for (int sample=1;sample<=3;++sample) timing.recordMicroseconds(sample,0);
    near(timing.averageMicroseconds(),2);require(timing.sampleCount()==3,"Timing warmup padded the window");
    for (int sample=4;sample<=10;++sample) timing.recordMicroseconds(sample,0);
    near(timing.averageMicroseconds(),5.5);timing.recordMicroseconds(11,0);near(timing.averageMicroseconds(),6.5);
    for (int sample=12;sample<=25;++sample) timing.recordMicroseconds(sample,0);
    near(timing.averageMicroseconds(),20.5);require(timing.sampleCount()==25,"Timing history lost chart samples");
    timing.recordMicroseconds(0,0);near(timing.averageMicroseconds(),18.9);
    // Old slow runs must disappear completely, not remain in a lifetime mean.
    timing={};timing.recordMicroseconds(1000,0);for (int n=0;n<10;++n) timing.recordMicroseconds(1,0);
    near(timing.averageMicroseconds(),1);
    timing={};for (int n=1;n<=75;++n) timing.recordMicroseconds(2*n,3*n);
    require(timing.sampleCount()==50,"Timing history is not bounded to 50 updates");
    for (std::size_t i=0;i<50;++i) {
        near(timing.sample(i).solverMicroseconds,2*(26+i));near(timing.sample(i).luaMicroseconds,3*(26+i));
        near(timing.sample(i).totalMicroseconds(),5*(26+i));
    }
    near(timing.average().solverMicroseconds,141);near(timing.average().luaMicroseconds,211.5);
    near(timing.averageMicroseconds(),352.5);
    using I=nsi::core::Installation;I scene(1,1);scene.setHullType(std::nullopt);scene.toggleWall(0,scene.level(0).horizontalWall(0,0));
    CellPlaybackRuntime runtime(scene);near(runtime.averageStepMicroseconds(),0);
    require(runtime.update(0.001),"Timed update failed");const double measured=runtime.averageStepMicroseconds();
    require(measured>0,"Successful update was not timed");
    require(runtime.stepTimings().sampleCount()==1 && runtime.stepTimings().sample(0).solverMicroseconds>0
        && runtime.stepTimings().sample(0).luaMicroseconds>0,"Separate solver/Lua timing was not recorded");
    near(measured,runtime.stepTimings().sample(0).totalMicroseconds());
    require(runtime.update(0),"Zero-duration update failed");require(runtime.averageStepMicroseconds()==measured,"Zero-duration call changed timing window");
    require(!runtime.update(0.1,0),"Timing failure fixture did not refuse");
    require(runtime.averageStepMicroseconds()==measured && runtime.stepTimings().sampleCount()==1,"Failed update changed timing window");
    SimulationPlayback playback;playback.reset(scene);playback.togglePlaying();playback.update(0.1);playback.togglePlaying();
    const double paused=playback.runtime()->averageStepMicroseconds();playback.update(10);
    require(playback.runtime()->averageStepMicroseconds()==paused,"Paused frame changed timing window");
    playback.setSpeedIndex(4);playback.togglePlaying();playback.update(0.1);
    require(playback.runtime()->stepTimings().sampleCount()==2,"Accelerated simulated substeps filled the wall-cadence timing chart");
    playback.reset(scene);near(playback.runtime()->averageStepMicroseconds(),0);
    require(playback.runtime()->stepTimings().sampleCount()==0,"Reset retained timing chart history");
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
    const auto pressure=p.pressureHistory(vented.atmospheres[0].id);
    require(pressure.back().pressureKPa<pressure.front().pressureKPa,"Pressure history missed Door decompression");
    near(pressure.back().pressureKPa,vented.atmospheres[0].pressureKPa);
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
    // The history under test is a cooling ramp, calibrated at zero solar
    // gain; a new document now authors a live sky, so ask for darkness.
    installation.setOrbit(std::nullopt);
    SimulationPlayback playback;
    playback.reset(installation);
    const auto initial=playback.runtime()->snapshot();
    for (const auto& air:initial.atmospheres) {
        const auto samples=playback.temperatureHistory(air.id);
        require(samples.size()==1,"History did not start at time zero");
        near(samples[0].seconds,0); near(samples[0].temperatureC,air.temperatureC);
        const auto pressures=playback.pressureHistory(air.id);
        require(pressures.size()==1,"Pressure history did not start at time zero");
        near(pressures[0].seconds,0); near(pressures[0].pressureKPa,air.pressureKPa);
        const auto composition=playback.atmosphereHistory(air.id);
        require(composition.size()==1 && composition[0].wetMoleBoundsPercent,"Composition history did not start at time zero");
        near(composition[0].relativeHumidity,air.relativeHumidity);
        near(composition[0].wetMoleBoundsPercent->back(),100);
    }
    require(playback.temperatureHistory("missing").empty(),"Unknown compartment gained history");
    require(playback.pressureHistory("missing").empty(),"Unknown compartment gained pressure history");
    require(playback.atmosphereHistory("missing").empty(),"Unknown compartment gained composition history");
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
            const auto pressures=playback.pressureHistory(air.id);
            require(pressures.size()==samples.size(),"Pressure samples differ from temperature cadence");
            const auto composition=playback.atmosphereHistory(air.id);
            require(composition.size()==samples.size(),"Composition history is not cadence-aligned and bounded");
            for (std::size_t n=0;n<composition.size();++n) {
                near(composition[n].seconds,samples[n].seconds);
                require(composition[n].wetMoleBoundsPercent.has_value(),"Ordinary atmosphere lost composition");
                near(composition[n].wetMoleBoundsPercent->back(),100);
            }
            require(composition.back().relativeHumidity>composition.front().relativeHumidity,"Humidity history missed cooling response");
            for (std::size_t n=0;n<samples.size();++n) near(pressures[n].seconds,samples[n].seconds);
            require(pressures.back().pressureKPa<pressures.front().pressureKPa,"Pressure history missed cooling response");
            if (std::abs(pressures.back().seconds-snapshot.elapsedSeconds)<1e-9)
                near(pressures.back().pressureKPa,air.pressureKPa);
        }
        const auto id=snapshot.atmospheres[0].id;
        const auto count=playback.temperatureHistory(id).size();
        const auto last=playback.temperatureHistory(id).back();
        const auto pressureLast=playback.pressureHistory(id).back();
        const auto humidityLast=playback.atmosphereHistory(id).back().relativeHumidity;
        playback.togglePlaying(); playback.update(100);
        require(playback.temperatureHistory(id).size()==count,"Paused graph added samples");
        near(playback.temperatureHistory(id).back().seconds,last.seconds);
        near(playback.temperatureHistory(id).back().temperatureC,last.temperatureC);
        require(playback.pressureHistory(id).size()==count,"Paused pressure graph added samples");
        near(playback.pressureHistory(id).back().pressureKPa,pressureLast.pressureKPa);
        require(playback.atmosphereHistory(id).size()==count,"Paused composition history added samples");
        near(playback.atmosphereHistory(id).back().relativeHumidity,humidityLast);
    }
    const auto id=initial.atmospheres[0].id;
    playback.reset(installation);
    require(playback.temperatureHistory(id).size()==1,"Reset retained old temperature graph");
    require(playback.pressureHistory(id).size()==1,"Reset retained old pressure graph");
    require(playback.atmosphereHistory(id).size()==1,"Reset retained composition graph");
    near(playback.temperatureHistory(id)[0].temperatureC,20);
    installation.setName("Replacement");
    playback.synchronize(installation);
    require(playback.temperatureHistory(id).size()==1,"Document replacement retained graph");
    require(playback.pressureHistory(id).size()==1,"Document replacement retained pressure graph");
    require(playback.atmosphereHistory(id).size()==1,"Document replacement retained composition graph");
    installation.setCell(0,0,0,I::SectorType::Void);
    playback.synchronize(installation);
    require(playback.temperatureHistory(id).empty(),"Removed compartment retained graph");
    require(playback.pressureHistory(id).empty(),"Removed compartment retained pressure graph");
    require(playback.atmosphereHistory(id).empty(),"Removed compartment retained composition graph");
    playback.synchronize(nullptr);
    for (const auto& air:initial.atmospheres) {
        require(playback.temperatureHistory(air.id).empty(),"Closed document retained graph");
        require(playback.pressureHistory(air.id).empty(),"Closed document retained pressure graph");
        require(playback.atmosphereHistory(air.id).empty(),"Closed document retained composition graph");
    }
}

void hullPlayback() {
    using I=nsi::core::Installation;
    auto temperature=[](I::HullType type,const std::optional<I::Orbit>& orbit) {
        station::editor::EditorSession session;
        session.newDocument(1,1);
        session.setHullType(type);
        session.setOrbit(orbit);
        station::editor::SimulationPlayback playback;
        playback.synchronize(session.installation());
        playback.setSpeedIndex(4);
        playback.togglePlaying();
        for (int frame=0;frame<600;++frame) playback.update(0.1);
        const auto state=playback.runtime()->snapshot();
        const auto& structure=playback.runtime()->structuralSnapshot();
        require(state.healthy && !structure.bodies.empty(),"Playback omitted local Hull panels");
        require(!state.thermal.hull && state.thermalLinks.empty(),"Playback published legacy Hull");
        require(structure.paths.exteriorRadiationJ>0,"Playback Hull did not radiate");
        return std::pair{state.atmospheres[0].temperatureC,structure.paths.absorbedSolarJ};
    };
    // The Hull drain links are calibrated at zero solar gain, so this
    // fixture asks for a cleared orbit. That darkness used to come from no
    // document having an orbit at all; now it has to be said out loud.
    const auto bare=temperature(I::HullType::BareSkin,std::nullopt);
    const auto blanket=temperature(I::HullType::MultiLayerBlanket,std::nullopt);
    require(bare.first<blanket.first-1.0 && blanket.first<20.0,
           "Playback did not wire Hull drain links");
    require(bare.second==0.0L && blanket.second==0.0L,
           "A cleared orbit did not keep the playback sky dark");
    // The inversion the default orbit brings: the same fixture under the
    // default ISS-like sky is lit, and BareSkin takes far more of it than
    // the blanket does.
    const I::Orbit iss{1361.0,5570.0,6791000.0};
    const auto litBare=temperature(I::HullType::BareSkin,iss);
    const auto litBlanket=temperature(I::HullType::MultiLayerBlanket,iss);
    require(litBare.second>0.0L && litBlanket.second>0.0L,
           "The default orbit did not light the playback sky");
    require(litBare.second>litBlanket.second,
           "Bare skin and the blanket absorbed the default sky identically");
}
void orbitPlayback() {
    using I=nsi::core::Installation;
    auto run=[](const std::optional<I::Orbit>& orbit) {
        station::editor::EditorSession session;
        session.newDocument(1,1);
        session.setHullType(I::HullType::BareSkin);
        session.setOrbit(orbit);
        require(session.installation().orbit()==orbit,"Session dropped the authored orbit");
        station::editor::SimulationPlayback playback;
        playback.synchronize(session.installation());
        playback.setSpeedIndex(4);
        playback.togglePlaying();
        for (int frame=0;frame<600;++frame) playback.update(0.1);
        const auto state=playback.runtime()->snapshot();
        const auto& structure=playback.runtime()->structuralSnapshot();
        require(state.healthy && !structure.bodies.empty(),"Orbit playback omitted local Hull panels");
        return std::pair{structure.paths.absorbedSolarJ,structure.paths.exteriorRadiationJ};
    };
    const auto dark=run(std::nullopt);
    require(dark.first==0.0L,"Dark control of the orbit playback fixture absorbed starlight");
    const auto lit=run(I::Orbit{1361.0,5570.0,6791000.0});
    require(lit.first>0.0L,"Authored orbit did not heat the playback hull");
    require(lit.second>0.0L,"Playback Hull did not radiate");
    // The same run opened part-way through an eclipse: the authored phase
    // offset puts the shadow over the whole first minute of playback,
    // where the unshifted run was in full daylight.
    const auto shifted=run(I::Orbit{1361.0,5570.0,6791000.0,0.3,23.44,50.0});
    require(shifted.first==0.0L,
           "The authored phase offset did not open playback inside the eclipse");
    require(shifted.second>0.0L,"The eclipsed playback Hull stopped radiating");
    // Tilted but unshifted: the axial tilt reaches the playback hull and changes
    // the load it takes, because the star is no longer overhead.
    const auto tilted=run(I::Orbit{1361.0,5570.0,6791000.0,0.3,23.44,0.0});
    require(tilted.first>0.0L,"A tilted orbit delivered no stellar energy to the playback hull");
    require(std::abs(double(tilted.first-lit.first))>1e-3*std::abs(double(lit.first)),
           "The axial tilt made no difference to the load the playback hull took");
}
void observationControls() {
    using namespace station::editor;using I=nsi::core::Installation;
    I scene(4,4);scene.setHullType(std::nullopt);scene.toggleWall(0,scene.level(0).horizontalWall(1,0));
    const auto authored=scene;CellPlaybackRuntime reference(scene),controlled(scene);
    require(controlled.observationOptions().windDiagnostics && controlled.observationOptions().objectForces,"Calculation defaults changed");
    for (bool wind:{true,false}) for (bool forces:{true,false}) {
        const auto clock=controlled.snapshot().elapsedSeconds;const auto transfers=controlled.snapshot().atmospherePathways[0].netTransferredKg;
        controlled.setObservationOptions({wind,forces});
        require(controlled.snapshot().elapsedSeconds==clock && controlled.snapshot().atmospherePathways[0].netTransferredKg==transfers,"Calculation toggle advanced/rebooked physics");
        require(controlled.update(0.1) && reference.update(0.1),"Observation toggle broke advance");
        for (std::size_t i=0;i<controlled.model().fluid().states().size();++i) {
            require(controlled.model().fluid().states()[i].q==reference.model().fluid().states()[i].q,"Diagnostics changed conserved gas state");
            const auto& c=controlled.snapshot().cellAtmospheres[i];
            require(c.windEnabled==wind && c.forcesEnabled==forces,"Calculation flags not published");
            if (!wind) require(!c.windAvailable && c.windSpeedMS==0 && c.velocityX==0 && c.velocityY==0,"Disabled wind retained stale observations");
            if (!forces) require(!c.forceAvailable && c.forceN==std::array<double,2>{},"Disabled forces retained stale observations");
            else require(c.forceN==reference.snapshot().cellAtmospheres[i].forceN,"Forces depended on optional wind diagnostics");
        }
        const auto force=controlled.objectForce(0,1);require(bool(force)==forces,"Object force sampling ignored its toggle");
        if (forces) {
            station::CellAerodynamicBody moving;const auto& p=controlled.model().fluid().primitives()[1];moving.velocityMS={p.velocityX,p.velocityY};
            const auto carried=controlled.objectForce(0,1,moving);
            require(carried && std::hypot(carried->dragN[0],carried->dragN[1])<1e-12,"Object sampling ignored relative velocity");
        }
    }
    require(!controlled.objectForce(99,0) && !controlled.objectForce(0,999),"Missing Cell invented object forces");
    SimulationPlayback playback;playback.setObservationOptions({false,true});playback.reset(scene);playback.togglePlaying();playback.update(0.1);
    const auto id=playback.runtime()->snapshot().atmospheres[0].id;const auto count=playback.temperatureHistory(id).size();
    playback.setObservationOptions({true,false});require(playback.temperatureHistory(id).size()==count,"Toggling observations added history");
    playback.reset(scene);require(playback.observationOptions()==station::CellObservationOptions{true,false}
        && !playback.runtime()->snapshot().cellAtmospheres[0].forcesEnabled,"Reset lost calculation controls");
    require(scene==authored,"Calculation options mutated authored Installation");
    CellPlaybackRuntime evacuation(scene);double initialLoad=0;
    for (const auto& c:evacuation.snapshot().cellAtmospheres) initialLoad=std::max(initialLoad,std::hypot(c.forceN[0],c.forceN[1]));
    for (int step=0;step<100;++step) require(evacuation.update(0.1),"Load decay scenario failed");
    double remainingLoad=0;for (const auto& c:evacuation.snapshot().cellAtmospheres)
        if (c.forceAvailable) remainingLoad=std::max(remainingLoad,std::hypot(c.forceN[0],c.forceN[1]));
    require(initialLoad>0 && remainingLoad<initialLoad*0.001,"Evacuated gas retained full-strength airflow load");
}
void nativeSnapshots() {
    using namespace station::editor;using I=nsi::core::Installation;
    I scene(5,5);scene.setHullType(std::nullopt);scene.toggleWall(0,scene.level(0).horizontalWall(2,0));
    const auto authored=scene;SimulationPlayback p;p.reset(scene);
    const auto initial=p.runtime()->snapshot();
    require(initial.cellAtmospheres.size()==25 && initial.atmospheres[0].spatial,"Playback did not use native Cell state");
    p.togglePlaying();p.update(0.1);const auto snapshot=p.runtime()->snapshot();const auto& a=snapshot.atmospheres[0];
    double mean=0,minimum=1e300,maximum=0,wind=0;std::array<double,4> mass{};
    for (const auto& c:snapshot.cellAtmospheres) {
        require(c.level==0 && c.sectorUid==scene.level(0).sectors()[0].uid,"Cell observation lost geometry identity");
        mean+=c.pressureKPa/25;minimum=std::min(minimum,c.pressureKPa);maximum=std::max(maximum,c.pressureKPa);
        wind=std::max(wind,std::hypot(c.velocityX,c.velocityY));for (int g=0;g<4;++g) mass[g]+=c.massKg[g];
        require(!c.resolvedGas || c.relativeHumidity<=1+1e-8,"Cell humidity is supersaturated");
    }
    near(mean,a.pressureKPa);near(minimum,a.minCellPressureKPa);near(maximum,a.maxCellPressureKPa);
    require(minimum<maximum && wind>0,"Native breach stayed uniformly mixed or produced no wind");
    near(mass[0],a.oxygenKg);near(mass[1],a.nitrogenKg);near(a.residualKJ,0);
    near((a.currentEnergyKJ+a.kineticEnergyKJ)*1000,p.runtime()->model().sectors()[0].totalEnergyJ);
    require(a.oxygenKg<initial.atmospheres[0].oxygenKg,"Absent Wall did not act as breach in playback");
    require(a.belowPressureLimit==(minimum<a.minPressureKPa),"Sector pressure warning ignored local Cell extrema");
    require(snapshot.atmospherePathways.size()==1 && snapshot.atmospherePathways[0].id.starts_with("breach-"),"Breach telemetry missing");
    near(snapshot.atmospherePathways[0].netTransferredKg[0],a.exteriorLostKg[0]);
    require(scene==authored,"Native playback changed authored geometry");
    const auto history=p.pressureHistory(a.id);require(history.back().minimumKPa && history.back().maximumKPa,"Cell extrema missing from history");
    near(*history.back().minimumKPa,minimum);near(*history.back().maximumKPa,maximum);
    CellPlaybackRuntime refused(scene);const auto before=refused.snapshot();
    require(!refused.update(0.1,0) && !refused.snapshot().healthy && !refused.diagnostics().empty(),"Native failure was not exposed");
    require(refused.snapshot().worldTick==0 && refused.snapshot().cellAtmospheres==before.cellAtmospheres,"Failed native update committed snapshot state");
    require(refused.update(0.1) && refused.snapshot().healthy && refused.diagnostics().empty(),"Native retry failed");
}
void localStructuralPlayback() {
    using namespace station::editor;using I=nsi::core::Installation;
    I scene(1,1);scene.setHullType(I::HullType::BareSkin);const auto authored=scene;
    SimulationPlayback playback(std::nullopt);playback.reset(scene);
    require(playback.editingAllowed() && !playback.runtime()->structuralSnapshot().bodies.empty(),"Local-solid Reset failed");
    const auto energy=playback.runtime()->structuralSnapshot().solidEnergyJ;
    playback.togglePlaying();playback.setSpeedIndex(4);playback.update(0.1);
    require(playback.runtime()->snapshot().elapsedSeconds==1 && !playback.editingAllowed(),"Accelerated local playback/lockout failed");
    require(playback.runtime()->snapshot().thermalLinks.empty() && !playback.runtime()->snapshot().thermal.hull,"Local playback published legacy topology");
    require(playback.runtime()->structuralSnapshot().solidEnergyJ<energy && scene==authored,"Local playback lost solids or mutated authoring");
    const auto paths=playback.runtime()->structuralSnapshot().paths;
    const auto rates=playback.runtime()->structuralSnapshot().rates;
    const auto surfaces=playback.runtime()->structuralCell({0,0,0});
    require(surfaces.floor.surfaces==1 && surfaces.ceiling.surfaces==1 && surfaces.sides.surfaces==4,"Editor directional structural contract missing");
    require(paths.exteriorRadiationJ>0 && rates.exteriorRadiationW>0,"Editor path publication missing");
    playback.togglePlaying();require(!playback.editingAllowed(),"Paused local playback unlocked edits");
    playback.update(.5);
    require(playback.runtime()->structuralSnapshot().paths==paths && playback.runtime()->structuralSnapshot().rates==rates,"Pause changed structural observations");
    playback.reset(scene);require(playback.runtime()->structuralSnapshot().paths==station::NativeThermalPaths{},"Reset retained heat paths");require(playback.editingAllowed() && playback.runtime()->structuralSnapshot().solidEnergyJ==energy,"Local Reset retained cooled energy");
    for (const double duration:{0.1,1.0}) {
        CellPlaybackRuntime runtime(scene,{},std::nullopt);require(runtime.update(0.1),"Local publication setup failed");
        const auto before=runtime.snapshot();const auto bodies=runtime.structuralSnapshot().bodies;
        const auto paths=runtime.structuralSnapshot().paths;const auto rates=runtime.structuralSnapshot().rates;
        require(!runtime.update(duration,4096,0),"Local facade ignored thermal refusal");
        require(runtime.snapshot().elapsedSeconds==before.elapsedSeconds && runtime.snapshot().worldTick==before.worldTick
            && runtime.snapshot().completedTicks==before.completedTicks && runtime.snapshot().cellAtmospheres==before.cellAtmospheres,"Refusal changed committed gas/Lua publication");
        require(runtime.structuralSnapshot().paths==paths && runtime.structuralSnapshot().rates==rates,"Facade published refused paths");
        for (std::size_t i=0;i<bodies.size();++i) require(bodies[i].energyJ()==runtime.structuralSnapshot().bodies[i].energyJ(),"Refusal changed solid publication");
        require(runtime.update(duration) && runtime.snapshot().elapsedSeconds==before.elapsedSeconds+duration,"Local facade retry failed");
    }
}
void injectorPlayback() {
    using namespace station::editor;using I=nsi::core::Installation;
    I scene(4,1);scene.setHullType(std::nullopt);const auto authored=scene.serialize();
    SimulationPlayback p;p.reset(scene);const auto initial=p.runtime()->snapshot();
    const GasInjector source{0,0,2,.2};const auto id=initial.atmospheres[0].id;
    require(p.editingAllowed(),"Fresh Reset locked authoring");
    p.update(.25,source);p.update(.25,source);
    near(p.runtime()->snapshot().cellAtmospheres[0].massKg[2],initial.cellAtmospheres[0].massKg[2]+.1);
    require(p.runtime()->snapshot().cellAtmospheres[1].massKg==initial.cellAtmospheres[1].massKg,"Paused injector diffused gas");
    require(p.runtime()->snapshot().elapsedSeconds==0 && p.runtime()->snapshot().worldTick==0,"Paused injection ran physics/Lua");
    require(!p.editingAllowed(),"Injected state at t=0 did not require Reset before geometry edits");
    require(p.atmosphereHistory(id).size()==1 && p.atmosphereHistory(id).back().seconds==0,"Paused injection duplicated simulation-time history");
    require(p.atmosphereHistory(id).back().wetMoleBoundsPercent!=atmosphereSample(0,initial.atmospheres[0]).wetMoleBoundsPercent,"Paused injection left stale composition history");
    const auto paused=p.runtime()->snapshot();p.update(.5);
    require(p.runtime()->snapshot()==paused,"Released paused injector kept adding gas");
    p.togglePlaying();p.update(.1,source);
    near(p.runtime()->model().sectors()[0].ledger.sourceKg[2],.12);
    require(p.runtime()->snapshot().cellAtmospheres[1].massKg[2]>initial.cellAtmospheres[1].massKg[2],"Playing injector did not spread within Sector");
    p.update(.1);near(p.runtime()->model().sectors()[0].ledger.sourceKg[2],.12);
    near(p.runtime()->snapshot().atmospheres[0].carbonDioxideKg,initial.atmospheres[0].carbonDioxideKg+.12);
    p.togglePlaying();const auto count=p.atmosphereHistory(id).size();p.update(.5,source);
    near(p.runtime()->model().sectors()[0].ledger.sourceKg[2],.22);near(p.runtime()->snapshot().elapsedSeconds,.2);
    require(p.atmosphereHistory(id).size()==count,"Paused injection appended duplicate timestamps");
    p.reset(scene);require(p.editingAllowed() && p.runtime()->snapshot()==initial,"Reset retained injected gas/energy or lockout");
    for (const int speed:{0,2,4}) {
        p.reset(scene);p.setSpeedIndex(speed);p.togglePlaying();p.update(.1,source);
        near(p.runtime()->model().sectors()[0].ledger.sourceKg[2],.1*SimulationPlayback::speeds[speed]*source.kgPerS);
    }
    p.togglePlaying();const auto before=p.runtime()->model().sectors()[0].ledger.sourceKg[2];p.update(.2,source);
    near(p.runtime()->model().sectors()[0].ledger.sourceKg[2]-before,.04); // pause ignores 10x speed
    require(scene.serialize()==authored,"Injector changed saved geometry");

    I voidCell(2,1);voidCell.setCell(0,1,0,I::SectorType::Void);p.reset(voidCell);
    const auto untouched=p.runtime()->snapshot();p.update(.2,GasInjector{0,1,0,.1});
    require(p.runtime()->snapshot()==untouched && p.editingAllowed(),"Geometric Void acquired gas");
    p.update(.2,GasInjector{20,0,0,.1});require(p.runtime()->snapshot()==untouched,"Invalid Level acquired gas");
    auto yaml=scene.serialize();yaml.replace(yaml.find("Earth-like"),10,"Void");I absent;absent.deserialize(yaml);
    p.reset(absent);p.update(.2,source);require(p.runtime()->snapshot().cellAtmospheres.empty() && p.editingAllowed(),"Void atmosphere preset acquired gas");

    I levels(std::vector<I::Level>{I::Level(2,1),I::Level(1,1)});levels.setHullType(std::nullopt);p.reset(levels);
    const auto lower=p.runtime()->snapshot().cellAtmospheres[0].massKg;
    const auto upper=p.runtime()->snapshot().cellAtmospheres[2].massKg[1];
    p.update(.1,GasInjector{1,0,1,.1});
    require(p.runtime()->snapshot().cellAtmospheres[0].massKg==lower,"Injector hit wrong Level");
    near(p.runtime()->snapshot().cellAtmospheres[2].massKg[1],upper+.01);
    const std::string lifecycleFailure=R"lua(return function(name)
        return {advance=function(self,tick) error('injector lifecycle refusal') end}
    end)lua";
    CellPlaybackRuntime refused(scene,{},lifecycleFailure);const auto baseline=refused.snapshot();
    require(!refused.update(.1,4096,4096,source) && refused.snapshot().cellAtmospheres==baseline.cellAtmospheres
        && refused.model().sectors()[0].ledger==nsi::systems::CellAtmosphereLedger{},"Lifecycle refusal committed injection");

    p.reset(scene);p.togglePlaying();bool failed=false;
    try {p.update(.1,GasInjector{0,0,0,1e308});} catch (const std::runtime_error&) {failed=true;}
    require(failed && !p.playing() && p.runtime()->snapshot().cellAtmospheres==initial.cellAtmospheres,"Failed injection changed committed gas");
    p.update(.2,source);require(p.runtime()->snapshot().cellAtmospheres==initial.cellAtmospheres,"Paused source mutated a failed-step checkpoint");
    p.setSpeedIndex(0);p.togglePlaying();failed=false;
    try {p.update(0,GasInjector{0,1,1,.01});} catch (const std::runtime_error&) {failed=true;}
    require(failed && p.runtime()->snapshot().worldTick==0,"Retry discarded/rescaled the refused injector or duration");
    p.reset(scene);p.update(.1,source);near(p.runtime()->model().sectors()[0].ledger.sourceKg[2],.02);
    p.synchronize(nullptr);p.update(.1,source);require(!p.runtime() && p.editingAllowed(),"Retired document retained injector state");
}
void failedPlaybackDebt() {
    using namespace station::editor;using I=nsi::core::Installation;
    I scene(5,5);scene.setHullType(std::nullopt);const auto g=scene.level(0);const auto door=g.horizontalWall(2,2);
    for (unsigned x=0;x<5;++x) scene.toggleWall(0,g.horizontalWall(x,2));scene.setDoor(0,door,true);
    auto yaml=scene.serialize();const auto preset=yaml.find("atmosphere: Earth-like");const auto line=yaml.rfind('\n',preset)+1;
    yaml.insert(yaml.find('\n',preset),"\n"+std::string(preset-line,' ')+"atmosphereLoads: {occupants: 1, sensibleHeatPerOccupantKW: 1e11}");
    scene.deserialize(yaml);
    {
    SimulationPlayback p(std::nullopt);p.reset(scene);p.togglePlaying();p.toggleDoor(0,door);
    bool failed=false;try {p.update(0.1);} catch (const std::runtime_error&) {failed=true;}
    require(failed && !p.playing() && p.runtime()->snapshot().elapsedSeconds==0,"Work refusal did not pause/roll back playback");
    require(p.temperatureHistory(p.runtime()->snapshot().atmospheres[0].id).size()==1,"Refused update added history");
    CellPlaybackRuntime shortStep(scene);shortStep.setDoorOpen(0,door,true);
    require(shortStep.update(0.01),"Short-step control did not isolate pending-time behavior");
    p.setSpeedIndex(0);p.togglePlaying();failed=false;
    try {p.update(0);} catch (const std::runtime_error&) {failed=true;}
    require(failed && !p.playing() && p.runtime()->snapshot().worldTick==0,"Failed simulation-time debt was dropped or rescaled");
    }
}
// The session is live from the first frame: the grid the editor opens on
// is a document, so playback needs no file operation to unlock it. What
// a cancelled or failed Open or Save still owes is that it left the
// Installation untouched - not that it left the session inert, because
// inert was never the point of the guard and there is no longer a state
// that means it.
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
    auto playOneStep = [&] {
        playback.synchronize(session.installation());
        if (!playback.playing()) playback.togglePlaying();
        playback.update(0.1);
    };

    // Live before anything has been opened or saved.
    playOneStep();
    require(playback.playing() && playback.runtime() != nullptr,
            "The startup Installation could not be played");
    require(!session.unsavedChanges(), "The startup Installation counted as changed");
    const auto startup = session.installation();

    // A cancelled Open changes nothing and costs nothing.
    require(session.open(dialogs, files).outcome == FileOutcome::Cancelled,
            "A cancelled Open did not report Cancelled");
    require(session.installation() == startup, "A cancelled Open touched the Installation");
    playOneStep();
    require(playback.playing() && playback.runtime() != nullptr,
            "A cancelled Open stopped the show");

    // A failed Open says so, loudly, and still does not replace what you
    // were holding. That guarantee used to ride on a flag saying the
    // session had no document; it is asserted against the Installation
    // itself now, which is the thing the reader actually cares about.
    dialogs.next = {FileOutcome::Success, "installation.yaml", {}};
    files.fail = true;
    require(session.open(dialogs, files).outcome == FileOutcome::Failed,
            "A failed Open did not report Failed");
    require(session.installation() == startup, "A failed Open replaced the Installation");
    playOneStep();
    require(playback.playing() && playback.runtime() != nullptr,
            "A failed Open stopped the show");
    require(session.save(dialogs, files).outcome == FileOutcome::Failed,
            "A failed Save did not report Failed");
    require(session.installation() == startup, "A failed Save replaced the Installation");
    require(session.path().empty(), "A failed Save picked up a path it never wrote");

    // A successful Open replaces the Installation and keeps running.
    files.fail = false;
    require(session.open(dialogs, files).outcome == FileOutcome::Success,
            "A successful Open did not report Success");
    require(session.installation() != startup,
            "A successful Open did not replace the Installation");
    require(session.path() == "installation.yaml", "A successful Open left no path");
    playOneStep();
    require(playback.playing() && playback.runtime()->snapshot().worldTick >= 1,
            "A loaded Installation did not run");

    // Retiring the Installation - which now only happens when the session
    // itself goes away - still tears the preview down rather than keeping
    // a station running that nobody is holding.
    playback.synchronize(nullptr);
    playback.togglePlaying();
    playback.update(0.1);
    require(!playback.playing() && !playback.runtime(),
            "Removing the Installation retained a running preview");

    // A New Installation with nothing on disk behind it is live and
    // playable, and a successful Save is what puts a path under it.
    EditorSession created;
    created.newDocument(1, 1);
    require(created.path().empty(), "A New Installation picked up a path");
    playback.synchronize(created.installation());
    playback.togglePlaying();
    playback.update(0.1);
    require(playback.playing() && playback.runtime()->snapshot().worldTick >= 1,
            "A New unsaved Installation could not run");
    EditorSession saved;
    require(saved.save(dialogs, files).outcome == FileOutcome::Success,
            "A successful Save did not report Success");
    require(!saved.path().empty(), "A successful Save left no path");
}
}

int main() {
    try {
        using namespace station::editor;
        lifecycleTickIntegration();
        rollingStepTiming();
        editingLockout();
        observationControls();
        nativeSnapshots();
        localStructuralPlayback();
        failedPlaybackDebt();
        injectorPlayback();
        doorControls();
        documentAvailability();
        hullPlayback();
        orbitPlayback();
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
        playback.update(0);
        require(playback.runtime()->snapshot().worldTick==10,"Catch-up discarded time debt");
        near(playback.runtime()->snapshot().elapsedSeconds,1.0);
        std::cout << "Simulation playback passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
