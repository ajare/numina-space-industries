#include "CellFluidFixture.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
using namespace station;
namespace { bool trackAllocations=false;std::size_t allocations=0; }
void* operator new(std::size_t n) {
    if (trackAllocations) ++allocations;
    if (void* p=std::malloc(n ? n : 1)) return p;throw std::bad_alloc();
}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
namespace {
void require(bool x,const char* msg) {if (!x) throw std::runtime_error(msg);}
std::array<double,7> sum(std::span<const CellFluidState> states) {
    std::array<double,7> total{};for (const auto& s:states) for (int i=0;i<7;++i) total[i]+=s.q[i];return total;
}
void near(double a,double b,double relative=1e-10) {require(std::abs(a-b)<=relative*std::max({1.0,std::abs(a),std::abs(b)}),"Fluid conservation mismatch");}
void prolongedDoorDecompression() {
    // Two equal rooms: the sealed room keeps the activity reference at one
    // atmosphere while its neighbour drains through a restricted exterior Door.
    auto mesh=fluid_test::earthLikeGrid(4,4);std::size_t door=SIZE_MAX;
    for (std::size_t i=0;i<mesh.faces.size();++i) {
        auto& f=mesh.faces[i];
        if (f.axis==0 && f.b!=SIZE_MAX && f.a%4==1) f.openFraction=0;
        if (f.axis==0 && f.a==4 && f.b==SIZE_MAX && f.sign==-1) {
            f.openFraction=0.3;f.dischargeCoefficient=0.7;door=i;
        }
    }
    // Unresolved inventory must also remain untouched while an unrelated
    // sealed room has acoustic activity; a global force gauge invents impulse.
    auto trace=mesh;
    for (std::size_t i=0;i<trace.cells.size();++i) {
        if (i%4<2) for (auto& q:trace.cells[i].initial.q) q*=1e-22;
        else trace.cells[i].initial.q[6]*=1.013;
    }
    trace.cells[3].initial.q[6]*=1.1;CellFluidSolver frozen(trace);
    require(frozen.advance(0.1),"Unrelated acoustic activity destabilised retained near-vacuum gas");
    for (std::size_t i=0;i<trace.cells.size();++i) if (i%4<2)
        require(frozen.states()[i].q==trace.cells[i].initial.q,"Pressure reference invented near-vacuum momentum");
    CellFluidSolver fluid(mesh);const auto initial=sum(fluid.states());
    std::array<double,7> exterior{};std::array<double,2> walls{};
    for (int step=0;step<1200;++step) {
        require(fluid.advance(0.1),"Prolonged Door decompression failed positivity refinement");
        for (int k=0;k<7;++k) exterior[k]+=fluid.faceTransfers()[door].q[k];
        for (int axis=0;axis<2;++axis) walls[axis]+=fluid.wallImpulse()[axis];
    }
    const auto current=sum(fluid.states());
    for (int g=0;g<4;++g) near(initial[g],current[g]+exterior[g]);
    near(initial[6],current[6]+exterior[6]);
    for (int axis=0;axis<2;++axis) near(current[4+axis]-initial[4+axis],walls[axis]-exterior[4+axis],1e-7);
    for (std::size_t i=0;i<fluid.states().size();++i) {
        if (i%4>=2) require(fluid.states()[i].q==mesh.cells[i].initial.q,"Venting disturbed the sealed room");
        else {
            require(fluid.primitives()[i].pressurePa<1e-6,"Door did not evacuate its room");
            require(fluid.states()[i].q[6]>0,"Decompression discarded retained gas energy");
        }
    }
}
void check() {
    CellFluidSolver resting(fluid_test::grid(64,64));const auto initial=sum(resting.states());
    require(resting.advance(10),"Resting update failed");require(resting.work().resting && resting.work().substeps==0,"Rest did unnecessary acoustic work");
    require(initial==sum(resting.states()),"Resting state drifted");
    auto mesh=fluid_test::grid(32,32);mesh.cells[16*32+16].initial.q[6]*=1.2;
    CellFluidSolver wave(mesh);const auto before=sum(wave.states());
    trackAllocations=true;const bool advanced=wave.advance(0.001);trackAllocations=false;
    require(advanced,"Wave update failed");require(allocations==0,"Fluid stepping allocated heap memory");
    require(wave.primitives()[16*32+17].velocityX>0,"Pressure gradient did not produce wind");
    near(wave.primitives()[0].pressurePa,101325,1e-13);
    require(wave.advance(0.1),"Wave propagation failed");const auto after=sum(wave.states());
    for (int i=0;i<4;++i) near(before[i],after[i]);near(before[6],after[6]);
    const auto saved=std::vector<CellFluidState>(wave.states().begin(),wave.states().end());
    require(!wave.advance(1,1) && !wave.diagnostic().empty(),"Work budget not enforced");
    for (std::size_t i=0;i<saved.size();++i) require(saved[i].q==wave.states()[i].q,"Failed advance mutated inventory");
    // Uniform source changes must not mark the entire calm background as
    // disturbed once one local breach wakes the acoustic solver.
    CellFluidSolver background(fluid_test::grid(32,32)),heated(fluid_test::grid(32,32));
    for (std::size_t i=0;i<heated.states().size();++i) {heated.addHeat(i,1000);heated.addSpecies(i,2,0.001,293.15);}
    background.setOpening(0,1);heated.setOpening(0,1);const auto heatedBefore=sum(heated.states());
    require(background.advance(0.01) && heated.advance(0.01),"Uniform-source breach failed");
    require(heated.work().faceEvaluations<5*(background.work().faceEvaluations+1),"Uniform sources invalidated the whole resting background");
    CellFluidSolver localSource(fluid_test::grid(32,32));localSource.addHeat(0,1000);localSource.addSpecies(0,2,0.001,293.15);
    localSource.setOpening(0,1);require(localSource.advance(0.01),"Localized source breach failed");
    require(localSource.work().faceEvaluations<5*(background.work().faceEvaluations+1),"A localized source became the global resting background");
    const auto heatedAfter=sum(heated.states());
    for (int g=0;g<4;++g) near(heatedBefore[g]-heatedAfter[g],heated.faceTransfers()[0].q[g]);
    near(heatedBefore[6]-heatedAfter[6],heated.faceTransfers()[0].q[6]);
    for (int axis=0;axis<2;++axis) near(heatedAfter[4+axis]-heatedBefore[4+axis],heated.wallImpulse()[axis]-heated.faceTransfers()[0].q[4+axis],1e-8);
    // A present partition blocks mass, energy and waves, even inside one group.
    mesh=fluid_test::grid(4,1);mesh.cells[0].initial.q[6]*=2;
    for (auto& f:mesh.faces) if (f.a==1 && f.b==2) f.openFraction=0;
    CellFluidSolver wall(mesh);require(wall.advance(0.1),"Partition update failed");
    require(wall.states()[2].q==mesh.cells[2].initial.q && wall.states()[3].q==mesh.cells[3].initial.q,"Wall transmitted fluid");
    // A finite empty Cell fills; it is not a reservoir/sink.
    mesh=fluid_test::grid(2,1);mesh.cells[1].initial={};CellFluidSolver empty(mesh);
    const auto filledBefore=sum(empty.states());require(empty.advance(0.1),"Vacuum fill failed");
    require(empty.primitives()[1].massKg>0 && empty.primitives()[1].pressurePa>0,"Empty Cell acted as infinite vacuum");
    near(sum(empty.states())[1],filledBefore[1]);near(sum(empty.states())[6],filledBefore[6]);
    // Aperture on a real face, shared conservative flux and exterior ledger.
    mesh=fluid_test::grid(8,8);mesh.faces[0].openFraction=0.3;mesh.faces[0].dischargeCoefficient=0.7;
    CellFluidSolver breach(mesh);const auto start=sum(breach.states());require(breach.advance(0.1),"Breach update failed");
    const auto end=sum(breach.states());const auto lost=breach.faceTransfers()[0];
    for (int i=0;i<4;++i) near(start[i]-end[i],lost.q[i]);near(start[6]-end[6],lost.q[6]);
    for (int axis=0;axis<2;++axis) near(end[4+axis]-start[4+axis],breach.wallImpulse()[axis]-lost.q[4+axis],1e-8);
    require(end[1]<start[1] && end[6]<start[6],"Breach did not discharge");
    auto tall=fluid_test::grid(1,1,2),shortMesh=fluid_test::grid(1,1);
    tall.faces[0].openFraction=shortMesh.faces[0].openFraction=0.3;
    CellFluidSolver high(tall),low(shortMesh);require(high.advance(0.001) && low.advance(0.001),"Height test failed");
    require(high.primitives()[0].pressurePa>low.primitives()[0].pressurePa,"Height failed to increase fluid storage");
    // Mixture transport is conservative and local, including zero species.
    mesh=fluid_test::grid(2,1);const double oxygen=mesh.cells[1].initial.q[1]*StationMolarMassOxygenKgPerMol/StationMolarMassNitrogenKgPerMol;
    mesh.cells[1].initial=makeCellFluidState({oxygen,0,0,0},293.15);
    CellFluidSolver mixing(mesh);const auto unmixed=sum(mixing.states());require(mixing.advance(0.01),"Mixture transport failed");
    require(mixing.states()[0].q[0]>0 && mixing.states()[1].q[1]>0,"No neighbour mixing");
    for (int i=0;i<4;++i) near(unmixed[i],sum(mixing.states())[i]);near(unmixed[6],sum(mixing.states())[6]);
    // Sources carry explicit internal + kinetic energy; added gas is at rest.
    mesh=fluid_test::grid(1,1);const auto nitrogen=mesh.cells[0].initial.q[1];
    mesh.cells[0].initial=makeCellFluidState({0,nitrogen,0,0},293.15,10,20);
    CellFluidSolver source(mesh);const auto moving=source.states()[0];
    const auto removed=source.removeSpecies(0,1,0.1*nitrogen);
    near(moving.q[6]-source.states()[0].q[6],removed.energyJ);
    auto p=cellFluidPrimitive(source.states()[0],12);near(p.temperatureK,293.15);near(p.velocityX,10);near(p.velocityY,20);
    const double beforeAddition=source.states()[0].q[6],momentum=source.states()[0].q[4];
    const double added=source.addSpecies(0,0,0.25,310);near(source.states()[0].q[6]-beforeAddition,added);
    near(source.states()[0].q[4],momentum);source.addHeat(0,1000);
    near(source.states()[0].q[6],beforeAddition+added+1000);
    near(source.primitives()[0].temperatureK,cellFluidPrimitive(source.states()[0],12).temperatureK);
    const auto finite=source.states()[0];bool rejected=false;
    try {source.addHeat(0,-2*finite.q[6]);} catch (const std::exception&) {rejected=true;}
    require(rejected && finite.q==source.states()[0].q,"Invalid heat changed state");
    mesh=fluid_test::grid(1,1);mesh.cells[0].initial={};CellFluidSolver vacuum(mesh);rejected=false;
    try {vacuum.addHeat(0,1);} catch (const std::exception&) {rejected=true;}
    require(rejected && vacuum.states()[0].q[6]==0,"Heating vacuum invented gas energy");
    // Condensation keeps the same latent equilibrium as compartments, but also
    // books changing gas capacity and the liquid's sensible/kinetic energy.
    mesh=fluid_test::grid(1,1);mesh.cells[0].initial=makeCellFluidState({0,nitrogen,0,0.1},273.15,12,0);
    CellFluidSolver phase(mesh);const auto wet=phase.states()[0];const auto liquid=phase.condenseVapour(0);
    require(liquid.kg>0,"Cold Cell did not condense");near(wet.q[3],phase.states()[0].q[3]+liquid.kg);
    near(wet.q[6]+liquid.latentJ,phase.states()[0].q[6]+liquid.carriedEnergyJ);
    near(phase.primitives()[0].velocityX,12);require(phase.primitives()[0].temperatureK>273.15,"Latent heat missing");
    require(phase.condenseVapour(0).kg<1e-12,"Condensation did not reach equilibrium");
    mesh.cells[0].initial=makeCellFluidState({0,nitrogen,0,0.1},0);
    CellFluidSolver coldLimit(mesh);const auto frozen=coldLimit.condenseVapour(0);
    require(frozen.kg>0 && std::isfinite(coldLimit.primitives()[0].temperatureK),"Cold-limit phase equilibrium failed");
    near(frozen.latentJ,coldLimit.states()[0].q[6]+frozen.carriedEnergyJ);
    // Reflection, shocks and repeated calls retain positivity and conservation.
    mesh=fluid_test::grid(16,4);for (unsigned y=0;y<4;++y) for (unsigned x=0;x<8;++x) mesh.cells[y*16+x].initial.q[1]*=100;
    for (auto& c:mesh.cells) c.initial=makeCellFluidState({0,c.initial.q[1],0,0},293.15);
    CellFluidSolver shock(mesh);const auto shockBefore=sum(shock.states());
    for (int step=0;step<20;++step) {
        const auto previous=sum(shock.states());require(shock.advance(0.05),"Repeated shock/reflection integration failed");
        const auto current=sum(shock.states());
        for (int axis=0;axis<2;++axis) near(current[4+axis]-previous[4+axis],shock.wallImpulse()[axis],1e-8);
    }
    const auto shockAfter=sum(shock.states());for (int i=0;i<4;++i) near(shockBefore[i],shockAfter[i]);near(shockBefore[6],shockAfter[6]);
    for (const auto& value:shock.primitives()) require(std::isfinite(value.temperatureK) && value.temperatureK>=0 && value.pressurePa>=0,"Nonphysical shock state");
    // A weak right-moving acoustic pulse travels at the actual mixture sound
    // speed (within one Cell), not a tuned game-speed pressure-equalization rate.
    mesh=fluid_test::grid(100,1);const auto ambient=cellFluidPrimitive(mesh.cells[0].initial,12);
    const double gamma=ambient.soundSpeed*ambient.soundSpeed*ambient.density/ambient.pressurePa;
    for (std::size_t i=0;i<mesh.cells.size();++i) {
        const double x=2*(i+0.5),delta=0.001*std::exp(-(x-50)*(x-50)/72);
        const double densityFactor=1+delta/gamma;
        mesh.cells[i].initial=makeCellFluidState({0,ambient.massKg*densityFactor,0,0},293.15*(1+delta)/densityFactor,
            ambient.soundSpeed*delta/gamma,0);
    }
    CellFluidSolver acoustic(mesh);require(acoustic.advance(0.1),"Acoustic pulse failed");
    double weightedPosition=0,pressureWeight=0;
    for (std::size_t i=0;i<mesh.cells.size();++i) {
        const double weight=std::max(0.0,acoustic.primitives()[i].pressurePa-ambient.pressurePa);
        weightedPosition+=2*(i+0.5)*weight;pressureWeight+=weight;
    }
    require(pressureWeight>0 && std::abs(weightedPosition/pressureWeight-(50+0.1*ambient.soundSpeed))<2,
        "Acoustic propagation speed is incorrect");
    // Topology cannot silently omit walls or connect independent Levels.
    mesh=fluid_test::grid(2,1);mesh.cells[1].level=1;rejected=false;
    try {CellFluidSolver bad(mesh);} catch (const std::exception&) {rejected=true;}
    require(rejected,"Vertical airflow topology accepted");mesh=fluid_test::grid(1,1);mesh.faces.pop_back();rejected=false;
    try {CellFluidSolver bad(mesh);} catch (const std::exception&) {rejected=true;}
    require(rejected,"Incomplete Cell boundary accepted");
    mesh=fluid_test::grid(1,1);mesh.cells[0].volumeM3=std::numeric_limits<double>::min();rejected=false;
    try {CellFluidSolver bad(mesh);} catch (const std::exception&) {rejected=true;}
    require(rejected,"Non-finite derived properties accepted");
    require(!source.advance(std::numeric_limits<double>::infinity()),"Infinite simulation duration accepted");
    require(source.states()[0].q==finite.q,"Invalid duration changed inventory");
}
}
int main() {try {prolongedDoorDecompression();check();std::cout<<"Cell fluids passed\n";return 0;}catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
