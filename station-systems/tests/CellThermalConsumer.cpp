#include <station/CellThermal.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace station;
namespace {
void require(bool x,const char* why) {if (!x) throw std::runtime_error(why);}
void near(long double a,long double b,long double tolerance=1e-10L) {
    require(std::abs(a-b)<=tolerance*std::max({1.0L,std::abs(a),std::abs(b)}),"Cell thermal value mismatch");
}
long double integrate(double step) {
    CellThermalSolver solver(2,{{0,1,100}});std::vector<long double> c{1000,2000},t{400,200};
    const auto count=static_cast<unsigned>(std::round(1/step));
    for (unsigned i=0;i<count;++i) {
        require(solver.solve(c,t,step),"Refinement solve failed");
        for (int j=0;j<2;++j) t[j]+=solver.heatJ()[j]/c[j];
    }
    return t[0]-t[1];
}
void check() {
    CellThermalSolver solver(2,{{0,1,100}});std::vector<long double> c{1000,2000},t{400,200};
    require(solver.solve(c,t,0.1),"Two-body thermal solve failed");
    const auto expected=100*0.1*200/(1+100*0.1*(1.0/1000+1.0/2000));
    near(solver.heatJ()[0],-expected);near(solver.heatJ()[1],expected);near(solver.transferredJ()[0],expected);
    require(solver.iterations()<=2,"Two-body solve used unnecessary iterations");
    near(solver.heatJ()[0]+solver.heatJ()[1],0);
    CellThermalSolver parallel(2,{{0,1,100},{0,1,50}});
    require(parallel.solve(c,t,0.1),"Parallel-edge star failed");
    const double parallelQ=15*200/(1+15*(1.0/1000+1.0/2000));
    near(parallel.heatJ()[0],-parallelQ);near(parallel.transferredJ()[0],2*parallel.transferredJ()[1]);
    const auto exact=200*std::exp(-0.15);
    require(std::abs(integrate(0.01)-exact)<std::abs(integrate(0.1)-exact)/4,"Thermal refinement did not converge");
    CellThermalSolver stiff(3,{{0,1,100},{1,2,200}});
    c={1e4L,1e-15L,1e5L};t={400,100,200};require(stiff.solve(c,t,0.1),"Trace-capacity solve failed");
    const auto trace=t[1]+stiff.heatJ()[1]/c[1];
    require(trace>=200 && trace<=400 && std::abs(stiff.heatJ()[1])<1e-10,"Trace node acquired fictitious heat capacity");
    long double sum=0;for (const double q:stiff.heatJ()) sum+=q;near(sum,0,1e-9);
    require(stiff.maxFluxResidualJ()<1e-7,"Trace mesh flux residual too large");
    CellThermalSolver cycle(4,{{0,1,100},{1,2,200},{2,3,10},{3,0,20}});
    require(cycle.solve(std::vector<long double>{1e4L,1e-15L,1e5L,1e6L},std::vector<long double>{400,100,200,300},0.1),"General sparse trace mesh failed");
    require(cycle.iterations()>1 && cycle.maxFluxResidualJ()<1e-7,"CG fallback failed its convergence budget");
    c[1]=0;require(stiff.solve(c,t,0.1),"Empty-node solve failed");
    for (const auto q:stiff.heatJ()) near(q,0);for (const auto q:stiff.transferredJ()) near(q,0);
    require(!solver.solve(std::vector<long double>{1000,2000},std::vector<long double>{400,200},0.1,0),"Iteration budget ignored");
    require(!solver.diagnostic().empty(),"Missing work diagnostic");for (const auto q:solver.heatJ()) near(q,0);
    // Sparse memory and work: one Hull with 4096 Cell contacts.
    std::vector<CellThermalLink> links;for (std::size_t i=0;i<4096;++i) links.push_back({i,4096,0.4});
    CellThermalSolver large(4097,std::move(links));c.assign(4097,1e4L);t.assign(4097,293.15L);c.back()=1e7L;t.back()=290;
    require(large.solve(c,t,0.1),"4096-Cell thermal solve failed");require(large.iterations()<10,"Sparse Hull star did not converge promptly");
    sum=0;for (const auto q:large.heatJ()) sum+=q;near(sum,0,1e-8);
    std::vector<CellThermalLink> graphLinks;
    for (std::size_t i=0;i<4096;++i) {
        graphLinks.push_back({i,4096,0.4});
        if (i%64<63) graphLinks.push_back({i,i+1,30});
        if (i+64<4096) graphLinks.push_back({i,i+64,30});
        c[i]=i%131==0 ? 1e-15L : 1e4L;t[i]=293.15L+20*std::sin(static_cast<double>(i));
    }
    CellThermalSolver graph(4097,std::move(graphLinks));require(graph.solve(c,t,0.1),"Large cyclic thermal graph failed");
    require(graph.iterations()<32 && graph.maxFluxResidualJ()<1e-6,"Large CG graph failed convergence/work bounds");
    sum=0;for (const auto q:graph.heatJ()) sum+=q;near(sum,0,1e-7);
    HullSettings h;const auto radiation=integrateCellHullRadiation(h,293.15,0,0.1);
    require(radiation && radiation->temperatureChangeK<0 && radiation->emittedJ>0,"Hull did not radiate");
    near(h.heatCapacityKJPerK*1000*radiation->temperatureChangeK,radiation->absorbedJ-radiation->emittedJ);
    h.sinkTemperatureC=100;const auto warming=integrateCellHullRadiation(h,293.15,0,0.1);
    require(warming && warming->temperatureChangeK>0 && warming->emittedJ<0,"Back radiation lost its sign");
}
}
int main() {try {check();std::cout<<"Cell thermal solver passed\n";return 0;}
catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
