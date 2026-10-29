// Serial integrated rollout evidence; never used as a wall-clock CTest assertion.
#include "CellPlaybackRuntime.h"
#include "StructuralCostFixtures.h"
#include "StructuralPanel.h"
#include "InstallationRenderMode.h"
#include "SectorThermalPanel.h"
#include "SimulationTimingPanel.h"
#include <imgui.h>
#include <implot.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <malloc.h>
#include <new>

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
namespace {
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
void require(bool x,const std::string& why) {if(!x) throw std::runtime_error(why);}
I sceneFor(const std::string& name,unsigned size) {
    auto scene=structural_cost::fixture(name=="sustained-vacuum" ? "breaches" : name,size);
    if(name=="repressurisation") {
        for(unsigned y=0;y<size;++y) scene.toggleWall(0,scene.level(0).verticalWall(size/2,y));
        scene.setDoor(0,scene.level(0).verticalWall(size/2,size/2),true);
    }
    return scene;
}
CellAtmosphereExpansion expansionFor(const I& scene,const std::string& name,CellThermalMode mode) {
    auto g=expandCellAtmosphere(scene,mode==CellThermalMode::LocalStructure);
    for(auto& c:g.mesh.cells) {
        if(name=="gradient" && c.gridIndex%scene.level(c.level).width()<scene.level(c.level).width()/2) {
            std::array<double,4> mass{};std::copy_n(c.initial.q.begin(),4,mass.begin());
            c.initial=station::makeCellFluidState(mass,313.15);
        }
        if(name=="sustained-vacuum") for(auto& q:c.initial.q) q*=1e-8;
        if(name=="repressurisation" && c.gridIndex%scene.level(c.level).width()>=scene.level(c.level).width()/2) c.initial={};
    }
    if(name=="repressurisation" && g.structure) for(const auto& a:g.structure->assemblies) for(unsigned s=0;s<2;++s)
        if(a.owners[s] && a.owners[s]->worldX>=static_cast<int>(scene.level(0).width()/2)) {
            auto& b=g.structure->bodies[a.bodies[s]];b=station::ThermalBody({b.capacityJPerK(),250});
        }
    return g;
}
void advance(CellPlaybackRuntime& runtime,double seconds) {
    if(!runtime.update(seconds)) throw std::runtime_error(runtime.model().diagnostic());
}
void prepare(CellPlaybackRuntime& runtime,const I& scene,const std::string& name) {
    if(name=="repressurisation") require(runtime.setDoorOpen(0,scene.level(0).verticalWall(scene.level(0).width()/2,scene.level(0).depth()/2),true),"Door refused");
    if(name=="cooling" || name=="sustained-breaches" || name=="sustained-vacuum" || name=="evacuated") advance(runtime,1);
    if(name=="evacuated") for(const auto& p:runtime.model().fluid().primitives()) require(!p.resolvedGas,"Drain did not evacuate");
}
// Real headless panel/grid colour generation, not GPU rendering. GUI uses the
// authored preset state, not controlled initial-state injection; see report.
struct GuiCost {double frameMs{}, presetHostUpdateMs{};};
GuiCost gui(const I& scene,CellThermalMode mode,double seconds) {
    SimulationPlayback p(std::nullopt,mode);p.setObservationOptions({true,true});p.reset(scene);
    p.setSpeedIndex(seconds==1 ? 4 : 2);p.togglePlaying();
    const auto updateStart=Clock::now();p.update(.1);const auto hostMs=ms(updateStart);p.togglePlaying();
    require(std::abs(p.runtime()->snapshot().elapsedSeconds-seconds)<1e-12,"GUI setup duration");
    StructuralPanelState state;state.sector=true;
    const auto& owner=p.runtime()->model().geometry().gasOwners.front();
    InstallationRenderSettings settings;settings.mode=mode==CellThermalMode::LocalStructure ? InstallationRenderMode::Structure : InstallationRenderMode::Thermal;
    double total=0;
    for(int frame=-2;frame<5;++frame) {
        const auto start=Clock::now();
        const auto colours=cellRenderColours(scene,0,settings,&p.runtime()->snapshot(),&p.runtime()->model());
        ImGui::NewFrame();ImGui::SetNextWindowSize({1200,2200});ImGui::Begin("Integrated gate");
        if(mode==CellThermalMode::LocalStructure) drawStructuralPanel(p,owner,state);
        else drawSectorThermal(sectorThermal(scene,0,0,&p.runtime()->snapshot()),{},{},{},{});
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
}
int main(int argc,char** argv) {
    try {
        require(argc<=3,"Usage: editor-integrated-thermal-benchmark [trials=5] [size=64]");
        const int trials=argc>1 ? std::stoi(argv[1]) : 5;
        const unsigned size=argc>2 ? std::stoul(argv[2]) : 64;
        require(trials>0 && trials<=100 && size>=8 && size<=64 && size%8==0,"Invalid trials/size");
        ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={1400,2400};io.DeltaTime=1.f/60;
        unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        std::cout<<std::setprecision(10)<<"# serial integrated gate; gas_model="<<station::CellFluidModelRevision<<"; trials="<<trials<<"; warmups=2; size="<<size<<"; schema=4; wind=true; forces=true; acoustic_budget=4096; thermal_budget=4096\n";
        std::cout<<"backend,model_revision,preset_revision,scenario,seconds,accepted,diagnostic,build_ms,prepare_ms,update_ms,max_update_ms,solver_ms,lifecycle_ms,publication_remainder_ms,gui_cpu_ms,preset_editor_update_ms,per_0.1s_ms,gas_cells,bodies,assemblies,solid_links,film_links,exterior_links,cavities,portals,cpp_live_bytes,cpp_build_peak_bytes,topology_bound_bytes,observation_payload_bytes,acoustic_steps,work,intervals,refinements,nonlinear,linear,nonlinear_residual_J,flux_residual_J,thermal_energy_residual_J,global_residual_J,cumulative_ledger_magnitude_J,solid_delta_J,elapsed_delta_s\n";
        for(const std::string name:{"rest","cooling","loaded","gradient","breaches","sustained-breaches","sustained-vacuum","evacuated","repressurisation","many-sectors","tall-offset"}) {
            const auto scene=sceneFor(name,size);
            for(double seconds:{.1,1.}) for(auto mode:{CellThermalMode::Geometry,CellThermalMode::LocalStructure}) {
                double build=0,prep=0,total=0,maximum=0,solver=0,lifecycle=0;
                std::size_t bytes=0,peak=0,acoustic=0,cells=0;
                CellStructuralThermalState state;long double solidDelta=0,heatScale=0;double elapsedDelta=0;
                std::optional<bool> accepted;std::string diagnostic;
                for(int trial=-2;trial<trials;++trial) {
                    const auto base=liveBytes;peakBytes=base;const auto start=Clock::now();
                    CellPlaybackRuntime runtime(scene,expansionFor(scene,name,mode),{true,true},std::nullopt,mode);
                    const double construction=ms(start);const auto resident=liveBytes-base,constructionPeak=peakBytes-base;
                    auto stamp=Clock::now();prepare(runtime,scene,name);const auto preparation=ms(stamp);
                    const auto energy=runtime.structuralSnapshot().solidEnergyJ;const auto time=runtime.model().elapsedSeconds();
                    stamp=Clock::now();const bool ok=runtime.update(seconds);const auto update=ms(stamp);
                    require(!accepted || *accepted==ok,"Nondeterministic acceptance");accepted=ok;
                    require(size!=8 || ok,"Small-matrix smoke refused");
                    if(!ok) diagnostic=runtime.model().diagnostic();
                    require(std::abs(runtime.model().elapsedSeconds()-time-(ok ? seconds : 0))<1e-12,"Lost physical time/rollback");
                    if(!ok) require(runtime.structuralSnapshot().solidEnergyJ==energy,"Refusal changed committed solid energy");
                    if(ok && mode==CellThermalMode::LocalStructure) {
                        // Numerical thermal closure is enforced by the real solver.
                        // Report cumulative global cancellation separately: the
                        // Sector net ledgers cannot reconstruct sum(abs(node heat)).
                        require(std::isfinite(runtime.structuralSnapshot().residualJ),"Nonfinite global energy closure");
                        require(runtime.structuralSnapshot().solidEnergyJ!=energy,"Active solids skipped");
                    }
                    if(trial>=0) {
                        build+=construction;prep+=preparation;total+=update;maximum=std::max(maximum,update);
                        if(ok) {
                            const auto timing=runtime.stepTimings().sample(runtime.stepTimings().sampleCount()-1);
                            solver+=timing.solverMicroseconds/1000;lifecycle+=timing.luaMicroseconds/1000;
                        }
                        bytes=std::max(bytes,resident);peak=std::max(peak,constructionPeak);
                        acoustic=runtime.model().work().substeps;cells=runtime.model().fluid().states().size();
                        state=runtime.structuralSnapshot();heatScale=cumulativeLedgerMagnitude(runtime);solidDelta=state.solidEnergyJ-energy;elapsedDelta=runtime.model().elapsedSeconds()-time;
                    }
                }
                const auto display=gui(scene,mode,seconds);const auto& w=state.work;
                std::cout<<(mode==CellThermalMode::LocalStructure ? "LocalStructure" : "Geometry")<<','
                    <<(state.enabled ? state.modelRevision : "legacy-native")<<','<<(state.enabled ? state.presetRevision : "legacy")<<','<<name<<','<<seconds<<','<<*accepted<<','<<diagnostic<<','
                    <<build/trials<<','<<prep/trials<<','<<total/trials<<','<<maximum<<',';
                if(*accepted) std::cout<<solver/trials<<','<<lifecycle/trials<<','<<(total-solver-lifecycle)/trials;
                else std::cout<<"NA,NA,NA";
                std::cout<<','<<display.frameMs<<','<<display.presetHostUpdateMs<<',';
                if(*accepted) std::cout<<total/trials*.1/seconds;else std::cout<<"NA";
                std::cout<<','<<cells<<','<<state.bodies.size()<<','<<state.assemblies<<','<<state.solidContacts<<','<<state.gasContacts<<','<<state.exteriorContacts<<','<<state.cavities<<','<<state.portals<<','
                    <<bytes<<','<<peak<<','<<state.topologyStorageBytes<<','<<state.observationIndexPayloadBytes<<','<<acoustic<<','<<w.work<<','<<w.intervals<<','<<w.refinements<<','<<w.nonlinearIterations<<','<<w.linearIterations<<','
                    <<w.nonlinearResidualJ<<','<<w.fluxResidualJ<<','<<w.energyResidualJ<<','<<state.residualJ<<','<<heatScale<<','<<solidDelta<<','<<elapsedDelta<<'\n';
                std::cout.flush();
            }
        }
        ImPlot::DestroyContext();ImGui::DestroyContext();
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
