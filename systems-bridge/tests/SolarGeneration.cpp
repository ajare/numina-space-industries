#include <nsi/systems/SolarGeneration.h>
#include <nsi/core/WorldPresets.h>
#include <station/Replay.h>
#include <serialization/BinarySerializer.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace nsi::systems;
using namespace station;
namespace {
void require(bool ok,const char* message) {if (!ok) throw std::runtime_error(message);}
void close(double actual,double expected) {require(std::abs(actual-expected)<1e-8,"Solar generation mismatch");}
// Full-sun output of the 60 m^2 triple-junction fixture, computed the same way the
// runtime derives it so exact-equality flags compare like for like.
const double kFullSunKW=60*0.85*0.32*0.85*1361.0/1000.0;
StationSystemsSettings settings() {
    StationSystemsSettings s;s.enabled=true;
    SolarArraySettings a;a.id="array";a.areaM2=60;s.solarArrays={a};
    // A dark legacy cycle must not suppress authored-sky derived generation.
    s.solarCycle=ProfileSettings{0.0};return s;
}
void replay(StationSystemsRuntime& run) {
    const auto text=serializeStationReplay(run);
    std::unique_ptr<StationSystemsRuntime> restored;std::string diagnostic;
    require(parseStationReplay(text,restored,&diagnostic),diagnostic.c_str());
    require(restored->snapshot()==run.snapshot(),"YAML replay changed snapshot");
    require(serializeStationReplay(*restored)==text,"Replay journal changed");
    auto output=serialization::BinarySerializer::toString();
    run.writeFaultReplay(*output);output->serialize();
    auto input=serialization::BinarySerializer::fromString(output->getSerializedString());input->deserialize();
    auto binary=StationSystemsRuntime::readFaultReplay(*input,run.effectiveSettings(),0.1f);
    require(binary->snapshot()==run.snapshot(),"Binary replay changed snapshot");
    // Held external input survives replay, including a command after the last tick.
    require(run.update(0.1) && restored->update(0.1) && binary->update(0.1),"Replay continuation failed");
    require(run.snapshot()==restored->snapshot() && run.snapshot()==binary->snapshot(),"Replay lost held input");
}
void worlds() {
    for (auto index:{0u,2u,7u}) {
        auto orbit=nsi::core::applyWorldPreset(nsi::core::Installation::Orbit{},index);
        StellarExposureEvaluator sky(orbit);
        StationSystemsRuntime run(settings(),StationSystemsUpdateMode::Variable);
        require(updateSystemsWithAuthoredSky(run,sky,0.1),"World step failed");
        close(run.snapshot().power.sources[0].availableKW,60*0.85*0.32*0.85*nsi::core::kWorldPresets[index].stellarIrradianceWPerM2/1000);
        require(run.snapshot().healthy,"World audit failed");
        replay(run);
    }
}
void boundaryChecks() {
    auto s=settings();
    auto second=s.solarArrays.front();second.id="second";s.solarArrays.push_back(second);
    auto legacy=second;legacy.id="legacy";legacy.areaM2.reset();legacy.capacityKW=99;
    s.solarArrays.push_back(legacy);
    StationSystemsRuntime run(s,StationSystemsUpdateMode::Variable);
    StellarExposureEvaluator sky(nsi::core::Installation::Orbit{});
    require(updateSystemsWithAuthoredSky(run,sky,0.1),"Shared sky step failed");
    close(run.snapshot().power.sources[0].availableKW,kFullSunKW);
    close(run.snapshot().power.sources[1].availableKW,kFullSunKW);
    close(run.snapshot().power.sources[2].availableKW,0);
    const auto journal=serializeStationReplay(run);
    for (double invalid:{0.0,-1.0,std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::max()})
        require(!updateSystemsWithAuthoredSky(run,StellarExposureEvaluator{},invalid),"Invalid step accepted");
    require(serializeStationReplay(run)==journal,"Refused duration changed sky input");
    StationSystemsRuntime fixed(s,0.1f);
    const auto fixedJournal=serializeStationReplay(fixed);
    require(!updateSystemsWithAuthoredSky(fixed,sky,0.1),"Fixed-mode tick accepted");
    require(serializeStationReplay(fixed)==fixedJournal,"Fixed refusal changed input");
    replay(run);
    // The irradiance journal also works for clients of the original fixed drive.
    require(fixed.setDirectSolarIrradianceWPerM2(1361) && fixed.advanceTo(0.2),"Fixed solar step failed");
    std::unique_ptr<StationSystemsRuntime> restored;std::string diagnostic;
    require(parseStationReplay(serializeStationReplay(fixed),restored,&diagnostic),diagnostic.c_str());
    require(restored->snapshot()==fixed.snapshot(),"Fixed replay lost sky input");
}
void eclipseAndDark() {
    nsi::core::Installation::Orbit orbit;orbit.orbitalPeriodS=360;
    StellarExposureEvaluator sky(orbit);
    StationSystemsRuntime run(settings(),StationSystemsUpdateMode::Variable);
    StationSystemsRuntime quiet(settings(),StationSystemsUpdateMode::Variable);
    bool full=false,dark=false,ramp=false,recovered=false;
    for (int i=0;i<3600;++i) {
        require(updateSystemsWithAuthoredSky(run,sky,0.1),"Orbit step failed");
        // Extra reporting must not affect the tick, its audit or its journal.
        (void)run.snapshot();(void)run.events();
        require(updateSystemsWithAuthoredSky(quiet,sky,0.1),"Quiet step failed");
        require(run.snapshot()==quiet.snapshot(),"Observation changed generation");
        const auto power=run.snapshot().power.sources[0].availableKW;
        const auto expected=kFullSunKW*(1-sky.evaluate(run.snapshot().elapsedSeconds-0.05).eclipse);
        close(power,expected);full|=power==kFullSunKW;dark|=power==0;
        ramp|=power>0 && power<kFullSunKW;recovered|=dark && power==kFullSunKW;
        require(run.snapshot().healthy,"Orbit audit failed");
    }
    require(full && dark && ramp && recovered,"Missing eclipse/penumbra recovery");
    require(serializeStationReplay(run)==serializeStationReplay(quiet),"Observation changed journal");
    replay(run);
    require(updateSystemsWithAuthoredSky(run,StellarExposureEvaluator{},0.1),"Absent Orbit failed");
    close(run.snapshot().power.sources[0].availableKW,0);
    replay(run);
    require(run.setDirectSolarIrradianceWPerM2(123),"Pending input refused");replay(run);
}
}
int main() {try {worlds();boundaryChecks();eclipseAndDark();} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
