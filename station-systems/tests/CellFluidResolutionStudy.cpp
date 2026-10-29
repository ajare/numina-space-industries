// Diagnostic only: refined meshes do not alter production Cell resolution.
// Fixed physical geometry: two 6 m x 8 m rooms, 3 m storage height. Aligned
// 0.9 m x 2 m openings centred at y=3 m, Cd=0.7. Open the exterior at t=0,
// the partition opening at t=1 s. Pure N2, 101325 Pa / 293.15 K, no sources.
// Report the same three 2 m x 2 m bottom-row patches at every resolution.
// These are convergence observations, not a claim that the finest grid is truth.
#include <station/CellFluid.h>
#include <station/CellAerodynamics.h>
#include <station/Configuration.h>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <chrono>
#include <stdexcept>
using namespace station;
int main(int argc,char** argv) {
    try {
    if(argc>3) throw std::invalid_argument("Usage: station-cell-fluid-resolution-study [h=2] [host-step=0.1]");
    const double h=argc>1 ? std::stod(argv[1]) : 2.;
    const double host=argc>2 ? std::stod(argv[2]) : .1;
    if(!(h==2 || h==1 || h==.5 || h==.25 || h==.125) || !(host==.1 || host==.01 || host==.001))
        throw std::invalid_argument("h must be 2, 1, 0.5, 0.25 or 0.125; host step 0.1, 0.01 or 0.001");
    const unsigned nx=std::lround(12/h),ny=std::lround(8/h),mid=nx/2;
    const double volume=3*h*h,faceArea=3*h;
    CellFluidMesh mesh;
    const double rho=101325*StationMolarMassNitrogenKgPerMol/(StationUniversalGasConstantJPerMolK*293.15);
    for(unsigned y=0;y<ny;++y) for(unsigned x=0;x<nx;++x)
        mesh.cells.push_back({volume,0,y*nx+x,x<mid?0u:1u,makeCellFluidState({0,rho*volume,0,0},293.15)});
    std::vector<std::pair<std::size_t,double>> inlets;
    auto aperture=[&](unsigned y) {return 2*std::max(0.,std::min((y+1)*h,3.45)-std::max(y*h,2.55))/faceArea;};
    for(unsigned y=0;y<ny;++y) for(unsigned x=0;x<=nx;++x) {
        CellFluidFace f;
        f.axis=0;f.areaM2=faceArea;
        if(x==0) f={y*nx,SIZE_MAX,0,-1,faceArea,0};
        else if(x==nx) f={y*nx+x-1,SIZE_MAX,0,1,faceArea,aperture(y),.7};
        else f={y*nx+x-1,y*nx+x,0,1,faceArea,x==mid?0.:1.,x==mid?.7:1.};
        if(x==mid && aperture(y)>0) inlets.push_back({mesh.faces.size(),aperture(y)});
        mesh.faces.push_back(f);
    }
    for(unsigned y=0;y<=ny;++y) for(unsigned x=0;x<nx;++x) {
        if(y==0) mesh.faces.push_back({x,SIZE_MAX,1,-1,faceArea,0});
        else if(y==ny) mesh.faces.push_back({(y-1)*nx+x,SIZE_MAX,1,1,faceArea,0});
        else mesh.faces.push_back({(y-1)*nx+x,y*nx+x,1,1,faceArea,1});
    }
    const auto start=std::chrono::steady_clock::now();CellFluidSolver fluid(std::move(mesh));
    std::array<long double,7> initial{},boundary{};std::array<long double,2> walls{};
    for(const auto& s:fluid.states()) for(int k=0;k<7;++k) initial[k]+=s.q[k];
    station::CellAerodynamicField field(nx*ny);
    std::cout<<std::setprecision(12)<<"h,host_step,time,patch,p_Pa,vx,vy,rho,Fx,Fy,cell_K,min_p,max_p,mass_residual,energy_residual,px_residual,py_residual\n";
    const int openStep=std::lround(1/host),probeStep=std::lround(1.1/host),endStep=std::lround(2/host);
    for(int i=1;i<=endStep;++i) {
        if(i==openStep+1) for(auto [f,a]:inlets) fluid.setOpening(f,a);
        if(!fluid.advance(host)) {std::cerr<<fluid.diagnostic()<<'\n';return 2;}
        for(std::size_t j=0;j<fluid.mesh().faces.size();++j) if(fluid.mesh().faces[j].b==SIZE_MAX)
            for(int k=0;k<7;++k) boundary[k]+=fluid.faceTransfers()[j].q[k];
        const auto wi=fluid.wallImpulse();walls[0]+=wi[0];walls[1]+=wi[1];
        if(i!=openStep && i!=probeStep && i!=endStep) continue;
        std::array<long double,7> sum{};for(const auto& s:fluid.states()) for(int k=0;k<7;++k) sum[k]+=s.q[k];
        field.update(fluid);
        for(unsigned patch=0;patch<3;++patch) {
            double p=0,vx=0,vy=0,m=0,Fx=0,Fy=0,n=0,T=0,minP=1e300,maxP=0;
            for(unsigned y=0;y<ny;++y) for(unsigned x=mid;x<nx;++x) if((x+.5)*h>=6+2*patch && (x+.5)*h<8+2*patch && (y+.5)*h>=6) {
                const auto& c=fluid.primitives()[y*nx+x];const auto load=field.sample(fluid,y*nx+x);
                p+=c.pressurePa;vx+=c.massKg*c.velocityX;vy+=c.massKg*c.velocityY;m+=c.massKg;
                Fx+=load.forceN[0];Fy+=load.forceN[1];T+=c.temperatureK;++n;minP=std::min(minP,c.pressurePa);maxP=std::max(maxP,c.pressurePa);
            }
            std::cout<<h<<','<<host<<','<<i*host<<','<<patch<<','<<p/n<<','<<vx/m<<','<<vy/m<<','<<m/(n*volume)<<','<<Fx/n<<','<<Fy/n<<','<<T/n<<','<<minP<<','<<maxP<<','
                <<sum[1]+boundary[1]-initial[1]<<','<<sum[6]+boundary[6]-initial[6]<<','<<sum[4]+boundary[4]-walls[0]-initial[4]<<','<<sum[5]+boundary[5]-walls[1]-initial[5]<<'\n';
        }
    }
    std::cerr<<"h="<<h<<" host_step="<<host<<" cells="<<nx*ny<<" elapsed_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<'\n';
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
