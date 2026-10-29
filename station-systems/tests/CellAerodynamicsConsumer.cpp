#include "CellFluidFixture.h"
#include <station/CellAerodynamics.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
namespace {bool tracking=false;std::size_t allocations=0;}
void* operator new(std::size_t n) {if(tracking)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace station;
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void near(double a,double b){require(std::abs(a-b)<1e-7,"Aerodynamic value mismatch");}
void check(){
    require(CellObservationOptions{}.windDiagnostics && CellObservationOptions{}.objectForces,"Observation defaults disabled");
    CellFluidPrimitive p;p.resolvedGas=true;p.density=1.2;p.velocityX=10;
    CellAerodynamicBody body;body.areaM2=2;body.dragCoefficient=0.5;body.volumeM3=3;
    auto force=cellAerodynamicLoad(p,{-2,3},body);
    require(force.available,"Resolved load unavailable");near(force.dynamicPressurePa,60);near(force.relativeSpeedMS,10);
    near(force.dragN[0],60);near(force.dragN[1],0);near(force.pressureN[0],6);near(force.pressureN[1],-9);
    near(force.forceN[0],66);near(force.forceN[1],-9);
    body.velocityMS={10,0};force=cellAerodynamicLoad(p,{-2,3},body);near(force.dragN[0],0);near(force.forceN[0],6);
    body.velocityMS={20,0};force=cellAerodynamicLoad(p,{},body);near(force.forceN[0],-60);
    body={};p.density*=1e-6;force=cellAerodynamicLoad(p,{},body);near(force.forceN[0],60e-6);
    p.velocityX=20;near(cellAerodynamicLoad(p,{},body).forceN[0],240e-6);
    p.resolvedGas=false;require(!cellAerodynamicLoad(p,{},body).available,"Unresolved gas invented load");
    body.areaM2=-1;bool threw=false;try{cellAerodynamicLoad(p,{},body);}catch(const std::invalid_argument&){threw=true;}
    require(threw,"Invalid body accepted");body={};p.resolvedGas=true;
    require(!cellAerodynamicLoad(p,{std::numeric_limits<double>::infinity(),0},body).available,"Infinite gradient accepted");
    p.density=std::numeric_limits<double>::max();require(!cellAerodynamicLoad(p,{},body).available,"Force overflow reported as zero load");
    auto mesh=fluid_test::grid(4,4);
    for(std::size_t i=0;i<mesh.cells.size();++i) mesh.cells[i].initial.q[6]*=(101325+6*double(i%4)-4*double(i/4))/101325;
    CellFluidSolver fluid(mesh);CellAerodynamicField field(mesh.cells.size());
    tracking=true;field.update(fluid);force=field.sample(fluid,5);tracking=false;
    require(allocations==0,"Aerodynamic update/sample allocated");near(field.pressureGradients()[5][0],3);near(field.pressureGradients()[5][1],-2);
    near(force.forceN[0],-3);near(force.forceN[1],2);
    for(std::size_t i=0;i<mesh.cells.size();++i) require(fluid.states()[i].q==mesh.cells[i].initial.q,"Sampling changed gas state");
    // A sealed neighbour cannot exert a fictitious force through its Wall.
    mesh=fluid_test::grid(2,1);mesh.cells[1].initial.q[6]*=10;
    for(auto& f:mesh.faces)if(f.b!=SIZE_MAX)f.openFraction=0;
    CellFluidSolver closed(mesh);CellAerodynamicField walls(2);walls.update(closed);
    near(walls.sample(closed,0).forceN[0],0);near(walls.sample(closed,1).forceN[0],0);
    mesh=fluid_test::grid(1,1);mesh.faces[0].openFraction=0.3;mesh.faces[0].dischargeCoefficient=0.7;
    CellFluidSolver door(mesh);CellAerodynamicField aperture(1);aperture.update(door);
    const double expected=door.primitives()[0].pressurePa*0.5*6*0.3*0.7/12;
    near(aperture.sample(door,0).forceN[0],-expected);
    door.setOpening(0,0);aperture.update(door);near(aperture.sample(door,0).forceN[0],0);
    mesh=fluid_test::grid(1,1,2);mesh.faces[0].openFraction=0.3;mesh.faces[0].dischargeCoefficient=0.7;
    CellFluidSolver tall(mesh);aperture.update(tall);near(aperture.sample(tall,0).forceN[0],-expected/2);
}
}
int main(){try{check();std::cout<<"Cell aerodynamics passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
