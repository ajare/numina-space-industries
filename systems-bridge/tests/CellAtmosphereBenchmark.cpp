#include <nsi/systems/CellAtmosphere.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
using namespace nsi::systems;
int main(int argc,char** argv) {
    const double seconds=argc>2 ? std::stod(argv[2]) : 0.1;
    bool ran=false;
    std::cout<<"4096 Cells; transport + loads + condensation + aggregation; thermal-* also include Hull conduction/radiation\n";
    for (const std::string scenario:{"rest","loaded","wave","breaches","sustained-breaches","loaded-breaches",
        "thermal-rest","thermal-wave","thermal-breaches","thermal-sustained-breaches","thermal-loaded-breaches"}) {
        if (argc>1 && scenario!=argv[1]) continue;
        ran=true;
        double total=0,maximum=0;
        for (int trial=0;trial<12;++trial) {
            nsi::core::Installation scene(64,64);const bool thermal=scenario.starts_with("thermal-");
            if (!thermal) scene.setHullType(std::nullopt);
            const bool breach=scenario.find("breaches")!=std::string::npos;
            if (breach) {
                const auto& g=scene.level(0);
                for (const auto w:{g.horizontalWall(0,0),g.horizontalWall(63,0),g.horizontalWall(0,64),g.horizontalWall(63,64)}) scene.toggleWall(0,w);
            }
            auto e=expandCellAtmosphere(scene);
            if (scenario.find("loaded")!=std::string::npos) {
                auto& a=e.atmospheres[0];a.occupants=4096;a.oxygenUsePerOccupantKgPerS=0.00001;
                a.co2ProductionPerOccupantKgPerS=0.000007;a.moistureAdditionPerOccupantKgPerS=0.000001;a.sensibleHeatPerOccupantKW=0.1;
            }
            if (scenario.ends_with("wave")) e.mesh.cells[32*64+32].initial.q[6]*=1.2;
            CellAtmosphereTransport gas(std::move(e),thermal ? CellThermalMode::LocalStructure : CellThermalMode::Adiabatic);
            if (scenario.find("sustained")!=std::string::npos && !gas.advance(1)) {std::cerr<<gas.diagnostic()<<'\n';return 1;}
            const auto start=std::chrono::steady_clock::now();
            if (!gas.advance(seconds)) {std::cerr<<gas.diagnostic()<<'\n';return 1;}
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if (trial>=2) {total+=ms;maximum=std::max(maximum,ms);}
        }
        std::cout<<scenario<<": "<<total/10<<" ms mean, "<<maximum<<" ms max / "<<seconds<<" simulated s\n";
    }
    if (!ran) {std::cerr<<"Unknown benchmark scenario\n";return 1;}
}
