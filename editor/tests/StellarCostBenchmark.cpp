// Serial stellar-exposure cost gate evidence; never used as a wall-clock
// CTest assertion.
//
// The orbital exposure model puts a pure evaluator on every thermal source
// slice. This tool measures what that costs against the standing rollout gate
// from #35, on the same 4,096-Cell fixture, with the same stage separation:
// construction, numerical work, lifecycle, the snapshot publication
// remainder and headless GUI cost. The only thing that varies between rows
// is the authored sky, so a row minus the dark-sky `rest` row at the same
// duration is the measured cost of the exposure model itself rather than an
// assumed one.
#include "CellPlaybackRuntime.h"
#include "ExposureSample.h"
#include "InstallationRenderMode.h"
#include "SectorThermalPanel.h"
#include "StellarExposureTrace.h"
#include "StructuralCostFixtures.h"
#include "StructuralPanel.h"
#include "SimulationTimingPanel.h"
#include <imgui.h>
#include <implot.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <map>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Linux-only allocation accounting, including scratch/rollback/publication C++
// allocations. Lua/SDL malloc and allocator overhead are NOT measured by this.
namespace { std::size_t liveBytes=0, peakBytes=0; }
void* operator new(std::size_t n) {
    void* p=std::malloc(n ? n : 1);if(!p) throw std::bad_alloc();
    liveBytes+=malloc_usable_size(p);peakBytes=std::max(peakBytes,liveBytes);return p;
}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {if(p) {liveBytes-=malloc_usable_size(p);std::free(p);}}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
void* operator new(std::size_t n,std::align_val_t a) {
    const auto alignment=static_cast<std::size_t>(a);
    void* p=std::aligned_alloc(alignment,((n ? n : 1)+alignment-1)/alignment*alignment);
    if(!p) throw std::bad_alloc();liveBytes+=malloc_usable_size(p);peakBytes=std::max(peakBytes,liveBytes);return p;
}
void* operator new[](std::size_t n,std::align_val_t a) {return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::align_val_t) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {::operator delete(p);}

using namespace station::editor;
using namespace nsi::systems;
using I=nsi::core::Installation;
using O=I::Orbit;
namespace {
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
double us(Clock::time_point start) {return std::chrono::duration<double,std::micro>(Clock::now()-start).count();}
void require(bool x,const std::string& why) {if(!x) throw std::runtime_error(why);}

// The compressed thermal cycle. The Orbital period is derived from the
// world's mass and the height above its surface, so a 60 s cycle is
// authored as the mass that makes that period true at ISS radius rather
// than as a typed period. The eclipse fraction - and with it the whole
// shape of the exposure profile, both crossings and the arcs between them
// - depends on the radius and the tilt alone, so the sky is exactly what a
// real LEO orbit has. Only the clock is compressed, which is what puts a
// complete cycle inside a measurement budget instead of an hour of it.
constexpr double kCyclePeriod=60.0;
constexpr double kIssRadius=6791000.0;
constexpr double kFullSun=1361.0;
constexpr double kEarthAlbedo=0.3;
// The critical tilt for an ISS-radius orbit is 69.7 deg; 80 deg is
// comfortably past it, so the sky never enters shadow and the hull stays in
// sustained direct light for the whole cycle.
constexpr double kNoEclipseTiltDegrees=80.0;
// The standing rollout gate carried forward from #35: 10 ms of wall clock
// per 0.1 simulated seconds. Recorded here so the verdict is computed from
// the measurement rather than asserted by hand.
constexpr double kGateTargetMsPer01s=10.0;
// The Editor's exposure history is bounded to one minute at a tenth of a
// second, so 600 samples is the most the trace ever has to resolve.
constexpr int kTraceHistorySamples=600;
// Iterations behind the isolated per-slice exposure figure. The work is a
// handful of transcendental calls per exterior face, so it needs a loop long
// enough to sit above the clock's own resolution.
constexpr int kExposureIterations=2000;

struct Scenario {
    std::string name;
    std::string sky;
    bool dark{};             // no Orbit authored at all
    double tiltDegrees{};
    bool centreOnCrossing{}; // prepare so the timed window straddles eclipse entry
    bool wholeCycle{};       // time a complete Orbital period of updates
};
const std::vector<Scenario> kScenarios={
    {"rest","no Orbit authored - dark sky - zero stellar term - evaluator never built",true,0.0,false,false},
    {"sustained-sunlit","Orbit at 80 deg axial tilt - past the shadow angle - sunlit all cycle",false,kNoEclipseTiltDegrees,false,false},
    {"eclipse-transition","Orbit at 0 deg axial tilt - timed window centred on the eclipse entry crossing",false,0.0,true,false},
    {"full-orbit","Orbit at 0 deg axial tilt - one complete cycle walked from Local noon",false,0.0,false,true},
    // Not a fifth sky. The dark-sky control measured again after the whole
    // matrix has run, so host drift across the run - governor, thermals,
    // cache residency - is visible in the same column as the exposure
    // overhead instead of being mistaken for it.
    {"rest-recheck","no Orbit authored - dark sky - re-measured last as the host-drift control",true,0.0,false,false},
};

// The fixture's sky: full Sun at Earth, an ISS-radius ride, and the world
// mass that makes the compressed 60 s cycle the Keplerian one at that
// radius - M = 4 pi^2 r^3 / (G T^2). The record is honest about the
// gravity it is pretending to, and every derived exposure quantity comes
// out as it would for a real low orbit.
O cycleSky(double tiltDegrees) {
    O sky;
    sky.stellarIrradianceWPerM2=kFullSun;
    sky.albedo=kEarthAlbedo;
    sky.axialTiltDeg=tiltDegrees;
    sky.planetRadiusM=nsi::core::PlanetRadiusM;
    sky.siderealRotationS=nsi::core::PlanetSiderealRotationS;
    sky.heightAboveGroundM=kIssRadius-nsi::core::PlanetRadiusM;
    sky.planetMassKg=4.0*nsi::core::kPi*nsi::core::kPi
                   *kIssRadius*kIssRadius*kIssRadius
                   /(nsi::core::GravitationalConstantM3PerKgS2*kCyclePeriod*kCyclePeriod);
    return sky;
}

I sceneFor(const Scenario& scenario,unsigned size) {
    // The same sealed 4,096-Cell MultiLayerBlanket fixture the #35 gate ran,
    // so a row here reads against a row there. Only the sky changes.
    auto scene=structural_cost::fixture("rest",size);
    if(scenario.dark) scene.setOrbit(std::nullopt);
    else scene.setOrbit(cycleSky(scenario.tiltDegrees));
    return scene;
}
double eclipseEntrySeconds(const CellAtmosphereTransport& model) {
    const auto& evaluator=model.exposureEvaluator();
    require(!evaluator.dark(),"eclipse-transition fixture came up dark");
    return (kCyclePeriod-evaluator.eclipseDurationSeconds())/2.0;
}
// Untimed preparation advances real simulated time through the production
// runtime; it is never a patch of live state.
double prepareSeconds(const Scenario& scenario,const CellAtmosphereTransport& model,double seconds) {
    if(!scenario.centreOnCrossing) return 0.0;
    return eclipseEntrySeconds(model)-seconds/2.0;
}
int measuredUpdates(const Scenario& scenario,double seconds) {
    if(!scenario.wholeCycle) return 1;
    const auto count=static_cast<int>(std::llround(kCyclePeriod/seconds));
    require(count>0,"A whole-cycle walk needs at least one update");
    return count;
}
std::string speedLabel(double seconds) {
    return seconds<=0.1 ? "1x" : (seconds-1.0)<1e-12 ? "10x" : std::to_string(seconds)+"x";
}
// The per-source-slice exposure work, measured on the fixture's own exterior
// face set: one evaluate() plus the per-face effective-sink, direct and
// reflected application the transport performs on every slice. This is the
// serial cost of the per-step exposure evaluator, isolated from the solver
// it feeds. The dark-sky row runs the same loop against a dark evaluator, so
// the difference between rows is the orbital arithmetic, not the loop.
double exposureStepMicroseconds(const CellAtmosphereTransport& model) {
    const auto& evaluator=model.exposureEvaluator();
    const auto faces=model.exteriorSolarFaces();
    const auto& hull=model.geometry().thermal.hull;
    const double deepSpaceK=hull?hull->sinkTemperatureC+station::StationCelsiusOffset:3.0;
    volatile double guard=0.0;
    const auto start=Clock::now();
    for(int i=0;i<kExposureIterations;++i) {
        const auto exposure=evaluator.evaluate(i*1e-3);
        for(const auto& face:faces) {
            guard+=face.effectiveSinkTemperatureK(evaluator,deepSpaceK);
            guard+=face.directW(evaluator,exposure);
            guard+=face.reflectedSunlightW(evaluator,exposure);
        }
    }
    return us(start)/kExposureIterations;
}
// The trace's own resolution cost at the history bound, without paying a
// minute of simulation to fill it.
double traceGeometryMicroseconds() {
    std::vector<ExposureSample> trace;
    trace.reserve(kTraceHistorySamples);
    for(int i=0;i<kTraceHistorySamples;++i)
        trace.push_back(ExposureSample{i*0.1,(i/37)%2==0 ? 1.0 : 0.0});
    volatile double guard=0.0;
    constexpr int rounds=20;
    const auto start=Clock::now();
    for(int r=0;r<rounds;++r) {
        const auto geometry=exposureTraceGeometry(trace);
        guard+=static_cast<double>(geometry.eclipseBands.size()+geometry.runs.size());
    }
    return us(start)/rounds;
}
// Real headless panel/grid colour generation, not GPU rendering. As in #35
// the GUI sample is built from the authored preset state with short populated
// histories; controlled initial-state preparation is not carried into it.
struct GuiCost {double frameMs{},presetHostUpdateMs{};};
GuiCost gui(const I& scene,double seconds) {
    SimulationPlayback p(std::nullopt);p.setObservationOptions({true,true});p.reset(scene);
    p.setSpeedIndex(seconds==1 ? 4 : 2);p.togglePlaying();
    const auto updateStart=Clock::now();p.update(.1);const auto hostMs=ms(updateStart);p.togglePlaying();
    require(std::abs(p.runtime()->snapshot().elapsedSeconds-seconds)<1e-12,"GUI setup duration");
    StructuralPanelState state;state.sector=true;
    const auto& owner=p.runtime()->model().geometry().gasOwners.front();
    InstallationRenderSettings settings;settings.mode=InstallationRenderMode::Structure;
    double total=0;
    for(int frame=-2;frame<5;++frame) {
        const auto start=Clock::now();
        const auto colours=cellRenderColours(scene,0,settings,&p.runtime()->snapshot(),&p.runtime()->model());
        ImGui::NewFrame();ImGui::SetNextWindowSize({1200,2200});ImGui::Begin("Stellar cost gate");
        drawStructuralPanel(p,owner,state);
        drawSimulationTiming(p.runtime()->stepTimings());ImGui::End();ImGui::Render();
        require(colours.size()==scene.level(0).cellCount(),"Grid colour count");
        if(frame>=0) total+=ms(start);
    }
    return {total/5,hostMs};
}
long double cumulativeLedgerMagnitude(const CellPlaybackRuntime& runtime) {
    const auto& p=runtime.structuralSnapshot().paths;
    long double sum=std::abs(p.gasIntoSolidJ)+p.solidConductionMagnitudeJ+p.interiorRadiationMagnitudeJ
        +std::abs(p.portalEscapeJ)+std::abs(p.exteriorRadiationJ)+std::abs(p.absorbedSolarJ);
    for(const auto& sector:runtime.model().sectors()) {
        const auto& l=sector.ledger;
        sum+=std::abs(l.inventoryEnergyJ)+std::abs(l.transportedEnergyJ)+std::abs(l.externalHeatJ)
            +std::abs(l.occupantHeatJ)+std::abs(l.latentHeatJ);
    }
    return sum;
}
// Everything one measured window leaves behind, averaged over its trials.
struct Row {
    std::string scenario,sky,speed,diagnostic;
    double seconds{};
    bool accepted{};
    double construction{},prepare{},update{},maximum{};
    double numerical{},lifecycle{},publication{};
    double gui{},guiHostUpdate{};
    double exposureStepUs{},exposureUpdateUs{},traceUs{};
    double per01{},overGate{},overheadVsRest{};
    bool hasRestBaseline{};
    int updates{};
    std::size_t faces{},cells{},bodies{},assemblies{},exteriorContacts{};
    std::size_t liveBytes{},buildPeakBytes{};
    std::size_t work{},intervals{},refinements{},nonlinearIters{},linearIters{};
    double maxNonlinearResidualJ{},maxFluxResidualJ{},energyResidualJ{},globalResidualJ{};
    long double absorbedSolarWindowJ{},ledgerMagnitude{},solidDeltaJ{};
    double eclipseFraction{},sunlitShare{},elapsedDelta{};
    std::size_t acoustic{};
};
}
int main(int argc,char** argv) {
    try {
        require(argc<=3,"Usage: editor-stellar-cost-benchmark [trials=5] [size=64]");
        const int trials=argc>1 ? std::stoi(argv[1]) : 5;
        const unsigned size=argc>2 ? std::stoul(argv[2]) : 64;
        require(trials>0 && trials<=100 && size>=8 && size<=64 && size%8==0,"Invalid trials/size");
        ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={1400,2400};io.DeltaTime=1.f/60;
        unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        const double traceUs=traceGeometryMicroseconds();
        std::cout<<std::setprecision(10)
            <<"# serial stellar exposure cost gate; gas_model="<<station::CellFluidModelRevision
            <<"; cycle_period_s="<<kCyclePeriod<<"; radius_m="<<kIssRadius
            <<"; gate_target_ms_per_0.1s="<<kGateTargetMsPer01s
            <<"; trials="<<trials<<"; warmups=2; size="<<size
            <<"; schema=4; wind=true; forces=true; acoustic_budget=4096; thermal_budget=4096\n";
        std::cout<<"scenario,sky,seconds,speed,accepted,diagnostic,"
            "construction_ms,prepare_ms,update_ms,max_update_ms,"
            "numerical_ms,lifecycle_ms,publication_remainder_ms,gui_cpu_ms,gui_host_update_ms,"
            "exposure_step_us,exposure_update_us,exposure_faces,"
            "per_0.1s_ms,gate_target_ms_per_0.1s_ms,gate_verdict,over_gate_ms,"
            "overhead_vs_rest_ms,updates,work,intervals,refinements,nonlinear,linear,"
            "max_nonlinear_residual_J,max_flux_residual_J,thermal_energy_residual_J,global_residual_J,"
            "absorbed_solar_window_J,solid_delta_J,cumulative_ledger_magnitude_J,acoustic_steps,"
            "eclipse_fraction,sunlit_share,"
            "gas_cells,bodies,assemblies,exterior_contacts,"
            "cpp_live_bytes,cpp_build_peak_bytes,exposure_trace_600_samples_us,elapsed_delta_s\n";
        std::map<double,double> restPer01;
        for(const auto& scenario:kScenarios) {
            const auto scene=sceneFor(scenario,size);
            for(double seconds:{.1,1.}) {
                // A whole-cycle walk is its own sample set: six hundred (or
                // sixty) updates give within-walk mean and max, so repeating
                // the whole orbit five more times would buy nothing but clock.
                const int cycleTrials=scenario.wholeCycle ? 1 : trials;
                const int warmups=scenario.wholeCycle ? 1 : 2;
                Row row;
                row.scenario=scenario.name;row.sky=scenario.sky;row.speed=speedLabel(seconds);
                row.seconds=seconds;row.traceUs=traceUs;
                std::optional<bool> windowAccepted;
                std::optional<int> refusalStep;
                double build=0,prep=0,total=0,maximum=0,numerical=0,lifecycle=0;
                std::size_t live=0,peak=0,completed=0;
                std::size_t work=0,intervals=0,refinements=0,nonlinearIters=0,linearIters=0;
                double maxNonlinear=0,maxFlux=0,energyResidual=0;
                long double absorbed=0,solidDelta=0;
                double exposureStep=0;
                for(int trial=-warmups;trial<cycleTrials;++trial) {
                    const auto base=liveBytes;peakBytes=base;
                    const auto start=Clock::now();
                    CellPlaybackRuntime runtime(scene,{true,true},std::nullopt);
                    const double construction=ms(start);
                    const auto resident=liveBytes-base,constructionPeak=peakBytes-base;
                    const auto prepareTo=prepareSeconds(scenario,runtime.model(),seconds);
                    require(prepareTo>=0.0,"Preparation target falls before the start of the cycle");
                    // Untimed preparation gets its own generous budget: it is
                    // a starting condition walked through the real runtime,
                    // not a throughput claim. The measured window below stays
                    // on the standing 4096 gate budgets.
                    const auto prepareStart=Clock::now();
                    const bool prepared=prepareTo>0.0
                        ? runtime.update(prepareTo,1u<<22,1u<<22) : true;
                    const double preparation=ms(prepareStart);
                    if(!prepared) {
                        const auto& playbackDiagnostics=runtime.diagnostics();
                        throw std::runtime_error("Preparation advance refused: "
                            +runtime.model().diagnostic()+" / "
                            +(playbackDiagnostics.empty()?"no playback diagnostic"
                                :playbackDiagnostics.back().message));
                    }
                    const auto startEnergy=runtime.structuralSnapshot().solidEnergyJ;
                    const auto startSolar=runtime.structuralSnapshot().paths.absorbedSolarJ;
                    const auto time=runtime.model().elapsedSeconds();
                    const auto count=measuredUpdates(scenario,seconds);
                    double updateTotal=0,updateMax=0,stepNumerical=0,stepLifecycle=0;
                    std::size_t stepWork=0,stepIntervals=0,stepRefinements=0;
                    std::size_t stepNonlinear=0,stepLinear=0,stepCompleted=0;
                    for(int u=0;u<count;++u) {
                        const auto stepStart=Clock::now();
                        const bool ok=runtime.update(seconds);
                        const double step=ms(stepStart);
                        if(!ok) {
                            // Determinism is about *where* the window stopped,
                            // not about the last step seen: a refusal at step 5
                            // in one trial and step 5 in the next is the same
                            // behaviour, and must not read as a divergence.
                            require(!refusalStep || *refusalStep==u,
                                "Nondeterministic refusal step across trials");
                            refusalStep=u;
                            require(std::abs(runtime.model().elapsedSeconds()
                                -(time+seconds*u))<1e-9,
                                "A refused step inside the window moved the clock");
                            row.diagnostic=runtime.model().diagnostic();
                            require(runtime.structuralSnapshot().solidEnergyJ==startEnergy,
                                "Refusal changed committed solid energy");
                            break;
                        }
                        require(std::abs(runtime.model().elapsedSeconds()
                            -(time+seconds*(u+1)))<1e-9,
                            "Lost physical time across the measured window");
                        require(std::isfinite(runtime.structuralSnapshot().residualJ),
                            "Nonfinite global energy closure");
                        const auto timing=runtime.stepTimings().sample(
                            runtime.stepTimings().sampleCount()-1);
                        stepNumerical+=timing.solverMicroseconds/1000;
                        stepLifecycle+=timing.luaMicroseconds/1000;
                        const auto& w=runtime.structuralSnapshot().work;
                        stepWork+=w.work;stepIntervals+=w.intervals;stepRefinements+=w.refinements;
                        stepNonlinear+=w.nonlinearIterations;stepLinear+=w.linearIterations;
                        maxNonlinear=std::max(maxNonlinear,w.nonlinearResidualJ);
                        maxFlux=std::max(maxFlux,w.fluxResidualJ);
                        energyResidual+=w.energyResidualJ;
                        ++stepCompleted;
                        updateTotal+=step;updateMax=std::max(updateMax,step);
                    }
                    if(trial<0) continue;
                    const bool windowOk=stepCompleted==static_cast<std::size_t>(count);
                    require(!windowAccepted || *windowAccepted==windowOk,
                        "Nondeterministic acceptance across trials");
                    windowAccepted=windowOk;
                    require(size!=8 || windowOk,"Small-matrix smoke refused");
                    build+=construction;prep+=preparation;
                    total+=updateTotal;maximum=std::max(maximum,updateMax);
                    numerical+=stepNumerical;lifecycle+=stepLifecycle;
                    work+=stepWork;intervals+=stepIntervals;refinements+=stepRefinements;
                    nonlinearIters+=stepNonlinear;linearIters+=stepLinear;
                    completed=stepCompleted;
                    live=std::max(live,resident);peak=std::max(peak,constructionPeak);
                    absorbed+=runtime.structuralSnapshot().paths.absorbedSolarJ-startSolar;
                    solidDelta+=runtime.structuralSnapshot().solidEnergyJ-startEnergy;
                    if(trial==cycleTrials-1) exposureStep=exposureStepMicroseconds(runtime.model());
                    row.cells=runtime.model().fluid().states().size();
                    row.faces=runtime.model().exteriorSolarFaces().size();
                    row.exteriorContacts=runtime.structuralSnapshot().exteriorContacts;
                    row.bodies=runtime.structuralSnapshot().bodies.size();
                    row.assemblies=runtime.structuralSnapshot().assemblies;
                    row.acoustic=runtime.model().work().substeps;
                    row.ledgerMagnitude=cumulativeLedgerMagnitude(runtime);
                    row.eclipseFraction=runtime.model().exposureEvaluator().dark()
                        ? std::numeric_limits<double>::quiet_NaN()
                        : runtime.model().exposureEvaluator().eclipseFraction();
                    row.sunlitShare=runtime.model().exposureEvaluator().dark()
                        ? std::numeric_limits<double>::quiet_NaN()
                        : 1.0-row.eclipseFraction;
                    row.elapsedDelta=runtime.model().elapsedSeconds()-time;
                    row.globalResidualJ=static_cast<double>(runtime.structuralSnapshot().residualJ);
                }
                row.accepted=*windowAccepted;
                row.construction=build/cycleTrials;row.prepare=prep/cycleTrials;
                row.update=total/cycleTrials;row.maximum=maximum;
                row.numerical=numerical/cycleTrials;row.lifecycle=lifecycle/cycleTrials;
                row.updates=static_cast<int>(completed);
                row.exposureStepUs=exposureStep;
                // One exposure evaluation per source slice, and the source
                // slice cap is 0.1 s, so a whole update pays it once per
                // tenth of a simulated second.
                row.exposureUpdateUs=row.exposureStepUs*std::max(1.0,std::ceil(seconds/0.1));
                row.work=work/cycleTrials;row.intervals=intervals/cycleTrials;
                row.refinements=refinements/cycleTrials;
                row.nonlinearIters=nonlinearIters/cycleTrials;row.linearIters=linearIters/cycleTrials;
                row.maxNonlinearResidualJ=maxNonlinear;row.maxFluxResidualJ=maxFlux;
                row.energyResidualJ=energyResidual/cycleTrials;
                row.absorbedSolarWindowJ=absorbed/cycleTrials;
                row.solidDeltaJ=solidDelta/cycleTrials;
                const auto display=gui(scene,seconds);
                row.gui=display.frameMs;row.guiHostUpdate=display.presetHostUpdateMs;
                if(row.accepted&&completed>0) {
                    row.publication=(row.update-row.numerical-row.lifecycle)/cycleTrials;
                    const auto covered=seconds*static_cast<double>(completed);
                    row.per01=row.update/covered*.1;
                    row.overGate=row.per01-kGateTargetMsPer01s;
                    if(scenario.name=="rest") restPer01[seconds]=row.per01;
                    const auto rest=restPer01.find(seconds);
                    if(rest!=restPer01.end()&&scenario.name!="rest") {
                        row.overheadVsRest=row.per01-rest->second;row.hasRestBaseline=true;
                    }
                }
                const std::string verdict=!row.accepted ? "refused"
                    : row.per01<=kGateTargetMsPer01s ? "pass" : "over-target-needs-review";
                std::cout<<row.scenario<<','<<row.sky<<','<<row.seconds<<','<<row.speed<<','
                    <<row.accepted<<','<<row.diagnostic<<','
                    <<row.construction<<','<<row.prepare<<','<<row.update<<','<<row.maximum<<','
                    <<row.numerical<<','<<row.lifecycle<<','
                    <<(row.accepted?std::to_string(row.publication):std::string("NA"))<<','
                    <<row.gui<<','<<row.guiHostUpdate<<','
                    <<row.exposureStepUs<<','<<row.exposureUpdateUs<<','<<row.faces<<','
                    <<(row.accepted?std::to_string(row.per01):std::string("NA"))<<','
                    <<kGateTargetMsPer01s<<','<<verdict<<','
                    <<(row.accepted?std::to_string(row.overGate):std::string("NA"))<<','
                    <<(row.hasRestBaseline?std::to_string(row.overheadVsRest):std::string("NA"))<<','
                    <<row.updates<<','<<row.work<<','<<row.intervals<<','<<row.refinements<<','
                    <<row.nonlinearIters<<','<<row.linearIters<<','
                    <<row.maxNonlinearResidualJ<<','<<row.maxFluxResidualJ<<','
                    <<row.energyResidualJ<<','<<row.globalResidualJ<<','
                    <<row.absorbedSolarWindowJ<<','<<row.solidDeltaJ<<','
                    <<row.ledgerMagnitude<<','<<row.acoustic<<','
                    <<row.eclipseFraction<<','<<row.sunlitShare<<','
                    <<row.cells<<','<<row.bodies<<','<<row.assemblies<<','<<row.exteriorContacts<<','
                    <<live<<','<<peak<<','<<row.traceUs<<','<<row.elapsedDelta<<'\n';
                std::cout.flush();
            }
        }
        ImPlot::DestroyContext();ImGui::DestroyContext();
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
