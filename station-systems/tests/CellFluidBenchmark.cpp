#include "CellFluidFixture.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
using namespace station;
int main() {
    std::cout<<"4096 Cells, four gases, single native thread; construction excluded; 2 warmups + 10 measured trials\n";
    for (const std::string scenario:{"rest","wave","breaches","sustained-breaches","fully-disturbed"}) {
        for (const double seconds:{0.01,0.05,0.1,0.2,1.0}) {
            if (scenario!="breaches" && seconds!=0.1) continue;
            double total=0,maximum=0;CellFluidWork work;
            for (int trial=0;trial<12;++trial) {
                auto mesh=fluid_test::earthLikeGrid(64,64);
                if (scenario=="wave") mesh.cells[32*64+32].initial.q[6]*=1.2;
                if (scenario=="fully-disturbed") for (std::size_t i=0;i<mesh.cells.size();++i)
                    mesh.cells[i].initial.q[6]*=1+0.05*std::sin(static_cast<double>(i));
                if (scenario=="breaches" || scenario=="sustained-breaches") for (std::size_t face:{std::size_t(0),std::size_t(64),std::size_t(63*65),std::size_t(64*65-1)})
                    mesh.faces[face].openFraction=1;
                CellFluidSolver solver(std::move(mesh));
                if (scenario=="sustained-breaches" && !solver.advance(1.0)) {std::cerr<<solver.diagnostic()<<'\n';return 1;}
                const auto start=std::chrono::steady_clock::now();
                if (!solver.advance(seconds)) {std::cerr<<solver.diagnostic()<<'\n';return 1;}
                const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                if (trial>=2) {total+=ms;maximum=std::max(maximum,ms);}work=solver.work();
            }
            std::cout<<std::fixed<<std::setprecision(3)<<scenario<<", "<<seconds<<" simulated s: "<<total/10<<" ms mean, "<<maximum
                <<" ms max; steps="<<work.substeps<<", faces="<<work.faceEvaluations<<", retries="<<work.retries<<'\n';
        }
    }
}
