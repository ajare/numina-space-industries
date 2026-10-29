#include "CellPlaybackRuntime.h"
#include "SectorThermalPanel.h"
#include "SimulationTimingPanel.h"
#include "StructuralCostModel.h"
#include <imgui.h>
#include <implot.h>
#include <chrono>
#include <iomanip>
#include <iostream>
using namespace structural_cost;
using namespace station::editor;
namespace {
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
double gui(const I& scene,const CellPlaybackRuntime& runtime) {
    const auto start=Clock::now();
    ImGui::NewFrame();ImGui::SetNextWindowSize({1000,1500});ImGui::Begin("Cost study");
    drawSectorThermal(sectorThermal(scene,0,0,&runtime.snapshot()),{},{},{},{});
    drawSimulationTiming(runtime.stepTimings());ImGui::End();ImGui::Render();return ms(start);
}
}
int main(int argc,char** argv) {
    try {
        require(argc<=2,"Usage: editor-structural-cost-benchmark [positive measured trials]");
        const int trials=argc==2 ? std::stoi(argv[1]) : 5;require(trials>0 && trials<=100,"trials must be 1..100");
        ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={1200,1800};io.DeltaTime=1.0f/60;
        unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        std::cout<<std::setprecision(9)<<"# serial; baseline=legacy-native; prototype=P1; material=C1-provisional; schema=4; trials="<<trials<<"; warmups=2\n";
        std::cout<<"backend,scenario,seconds,build_ms,update_ms,max_update_ms,solver_ms,lifecycle_ms,publication_remainder_ms,gui_cpu_ms,per_0.1s_ms,bodies,panels,links,cavities,bytes,intervals,nonlinear,cg,matvec,residual_J,energy_residual_J,acoustic_steps,legacy_thermal_iterations,through_links,film_links,local_links\n";
        for (const std::string scenario:{"rest","loaded","breaches","sustained-breaches","evacuated","many-sectors","tall-offset","gradient"}) {
            const auto scene=fixture(scenario);
            for (double seconds:{0.1,1.0}) {
                // Default baseline: both Wind and reference-object loads enabled.
                // Gradient initialization is not exposed by the production facade.
                if (scenario!="gradient") {
                    double build=0,total=0,maximum=0,solver=0,lua=0,display=0;std::size_t acoustic=0,thermal=0,nodes=0,contacts=0;
                    for (int trial=-2;trial<trials;++trial) {
                        auto start=Clock::now();CellPlaybackRuntime runtime(scene,{true,true});const double construction=ms(start);
                        if (scenario=="sustained-breaches" || scenario=="evacuated") require(runtime.update(1),"baseline preconditioning refused");
                        if (scenario=="evacuated") for (const auto& p:runtime.model().fluid().primitives()) require(!p.resolvedGas,"evacuated baseline still has resolved gas");
                        start=Clock::now();require(runtime.update(seconds),"baseline update refused");const double elapsed=ms(start);
                        const auto timing=runtime.stepTimings().sample(runtime.stepTimings().sampleCount()-1);
                        const double frame=gui(scene,runtime);
                        if (trial>=0) {build+=construction;total+=elapsed;maximum=std::max(maximum,elapsed);
                            solver+=timing.solverMicroseconds/1000;lua+=timing.luaMicroseconds/1000;display+=frame;
                            acoustic=runtime.model().work().substeps;thermal=runtime.model().thermalIterations();
                            nodes=runtime.model().fluid().states().size()+(runtime.model().thermal().hull ? 1 : 0);contacts=runtime.model().geometry().thermalContacts.size();}
                    }
                    std::cout<<"legacy-native,"<<scenario<<','<<seconds<<','<<build/trials<<','<<total/trials<<','<<maximum<<','<<solver/trials<<','<<lua/trials<<','<<(total-solver-lua)/trials<<','<<display/trials<<','<<total/trials*0.1/seconds
                        <<','<<nodes<<",0,"<<contacts<<",0,NA,NA,NA,NA,NA,NA,NA,"<<acoustic<<','<<thermal<<",NA,NA,NA\n";
                }
                for (int mode=0;mode<3;++mode) {
                    double build=0,total=0,maximum=0;std::size_t bodies=0,panels=0,links=0,cavities=0,bytes=0,through=0,films=0,local=0;Work work;
                    for (int trial=-2;trial<trials;++trial) {
                        auto start=Clock::now();Model model(scene,mode,scenario=="evacuated",scenario=="gradient");const double construction=ms(start);
                        start=Clock::now();model.advance(seconds);const double elapsed=ms(start);
                        if (trial>=0) {build+=construction;total+=elapsed;maximum=std::max(maximum,elapsed);work=model.work;
                            bodies=model.bodies.size();panels=model.panels;links=model.links.size();cavities=model.cavities.size();bytes=model.bytes();
                            through=model.throughLinks;films=model.filmLinks;local=model.localLinks;}
                    }
                    std::cout<<(mode==0 ? "P1-two-side" : mode==1 ? "P1-local" : "P1-cavity")<<','<<scenario<<','<<seconds<<','<<build/trials<<','<<total/trials<<','<<maximum
                        <<','<<total/trials<<",NA,NA,NA,"<<total/trials*0.1/seconds<<','<<bodies<<','<<panels<<','<<links<<','<<cavities<<','<<bytes<<','<<work.intervals<<','<<work.nonlinear<<','<<work.cg<<','<<work.matvec<<','<<work.residualJ<<','<<work.energyResidualJ<<",NA,NA,"<<through<<','<<films<<','<<local<<'\n';
                }
            }
        }
        ImPlot::DestroyContext();ImGui::DestroyContext();
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
