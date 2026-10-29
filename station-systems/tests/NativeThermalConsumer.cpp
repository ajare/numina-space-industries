#include <station/NativeThermal.h>
#include <station/DiffuseRadiation.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numeric>

namespace {
std::atomic<std::size_t> allocations{};
bool tracking=false;
void require(bool ok,const char* what) {if(!ok) {std::cerr<<what<<'\n';std::exit(1);}}
void near(double a,double b,double tolerance,const char* what) {require(std::abs(a-b)<=tolerance,what);}
using namespace station;
void audit(const NativeThermalSolver& s) {
    const auto heat=s.heatJ();
    double sum=0,scale=0; for(auto q:heat) {sum+=q;scale+=std::abs(q);}
    near(sum,s.report().exteriorHeatJ,1e-7+1e-12*scale,"energy audit");
    require(s.report().nonlinearResidualJ<=1e-7+1e-12*scale,"nonlinear audit");
}
// Matrix-free eliminated two-surface cavity surrogate, NOT production cavity physics.
struct PairOperator:ThermalAlgebraicOperator {
    bool freeze(std::span<const long double>,std::span<long double> d,std::span<long double> s) override {
        d[0]=d[1]=10; std::fill(s.begin(),s.end(),0);return true;
    }
    void apply(std::span<const long double> t,std::span<long double> p) const override {
        std::fill(p.begin(),p.end(),0);p[0]=10*(t[0]-t[1]);p[1]=-p[0];
    }
};
}
void* operator new(std::size_t n) {if(tracking) ++allocations;if(auto p=std::malloc(n?n:1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
int main() {
    using namespace station;
    std::vector<ThermalBody> bodies{ThermalBody({200,400}),ThermalBody({300,200})};
    std::vector<NativeThermalGas> gas{{100,30000,101325./300,{.21,.79,0,0},true}};
    NativeThermalSolver s(2,1,{{0,1,20}},{{0,0,2,{}},{1,0,2,{}}},{});
    const double initial=170000;
    for(int i=0;i<200;++i) {
        require(s.solve(bodies,gas,{},1),"isolated solve");audit(s);
        for(std::size_t j=0;j<2;++j) bodies[j].addHeat(s.heatJ()[j]);
        gas[0].energyJ+=s.heatJ()[2];
    }
    near(bodies[0].energyJ()+bodies[1].energyJ()+gas[0].energyJ,initial,2e-7,"isolated retained energy");
    near(bodies[0].temperatureK(),initial/600,1e-5,"weighted equilibrium body");
    near(gas[0].energyJ/100,initial/600,1e-5,"weighted equilibrium gas");
    // Arbitrary sparse cycle, disconnected local body, no hidden isothermal hub.
    bodies={ThermalBody({100,400}),ThermalBody({100,200}),ThermalBody({100,300}),ThermalBody({100,123})};
    NativeThermalSolver cycle(4,0,{{0,1,5},{1,2,5},{2,0,5}},{},{});
    require(cycle.solve(bodies,{},{},.1),"cycle solve");audit(cycle);
    require(cycle.temperatureK()[0]>390 && cycle.temperatureK()[1]<210,"persistent local gradient");
    near(cycle.temperatureK()[3],123,0,"disconnected patch unchanged");
    auto same=bodies; same.emplace_back(NativeThermalBodySettings{100,123});
    NativeThermalSolver addition(5,0,{}, {},{});
    require(addition.solve(same,{},{},.1),"same temperature addition");
    near(addition.temperatureK()[4],123,0,"addition has no impulse");
    // Pressure changes ONLY gas contact. Radiation and solid conduction continue.
    bodies.erase(bodies.begin()+2,bodies.end());gas[0]={100,30000,0,{.21,.79,0,0},true};
    NativeThermalSolver vacuum(2,1,{{0,1,5}},{{0,0,2,{}}},{{1,ThermalRadiationBoundary::Exterior,0,2,.8,.5}});
    const std::array boundary{NativeThermalExterior{3,10}};
    require(vacuum.solve(bodies,gas,boundary,.1),"vacuum solve"); audit(vacuum);
    near(vacuum.heatJ()[2],0,0,"zero pressure film detaches");
    require(vacuum.heatJ()[0]<0 && vacuum.report().exteriorHeatJ<0,"solids still conduct and radiate");
    gas[0].resolved=false;gas[0].energyJ=1e-9;
    require(vacuum.solve(bodies,gas,boundary,.1),"unresolved retained inventory");
    near(vacuum.heatJ()[2],0,0,"unresolved gas detaches");
    gas[0]={100,30000,101325./300,{.21,.79,0,0},true};
    require(vacuum.solve(bodies,gas,boundary,.1),"resolved film");
    require(vacuum.heatJ()[2]>0,"gas heats from local surface");
    const auto airHeat=vacuum.heatJ()[2];
    gas[0].wetMoleFractions={0,0,1,0};
    require(vacuum.solve(bodies,gas,boundary,.1),"current mixture film");
    require(vacuum.heatJ()[2]!=airHeat,"mixture affects film");
    // Last successful outputs survive early AND late refusal at accelerated duration.
    const std::vector saved(vacuum.heatJ().begin(),vacuum.heatJ().end());
    const auto intervalWork=vacuum.report().work;
    NativeThermalBudget budget;budget.maxWork=0;
    require(!vacuum.solve(bodies,gas,boundary,1,budget),"early budget refusal");
    require(std::equal(saved.begin(),saved.end(),vacuum.heatJ().begin()),"early atomic refusal");
    budget.maxWork=intervalWork+2;
    require(!vacuum.solve(bodies,gas,boundary,1,budget),"late budget refusal");
    require(vacuum.report().intervals>0,"late work actually advanced internally");
    require(std::equal(saved.begin(),saved.end(),vacuum.heatJ().begin()),"late atomic refusal");
    gas[0].energyJ=-1; require(!vacuum.solve(bodies,gas,boundary,.1),"negative inventory refusal");
    gas[0].energyJ=30000;gas[0].wetMoleFractions={1,1,0,0};
    require(!vacuum.solve(bodies,gas,boundary,.1),"bad mixture refusal");
    require(!vacuum.solve(bodies,gas,boundary,std::numeric_limits<double>::infinity()),"invalid time refusal");
    // Analytic one-body radiation cooling, refinement/convergence vs exact ODE.
    auto cool=[](double step,std::size_t iterations,std::size_t* refinements=nullptr) {
        std::vector<ThermalBody> b{ThermalBody({100,1000})};
        NativeThermalSolver r(1,0,{}, {},{{0,ThermalRadiationBoundary::Exterior,0,1,1,0}});
        NativeThermalBudget lim;lim.maxIntervalSeconds=step;lim.maxNonlinearIterations=iterations;lim.maxWork=1000000;
        std::array ext{NativeThermalExterior{0,0}};
        require(r.solve(b,{},ext,.1,lim),"nonlinear radiation convergence");audit(r);
        if(refinements) *refinements=r.report().refinements;
        return r.temperatureK()[0];
    };
    const double exact=std::pow(std::pow(1000.,-3)+3*5.670374419e-8*.1/100,-1./3);
    const auto coarse=cool(.1,32),fine=cool(.025,32);
    require(std::abs(fine-exact)<std::abs(coarse-exact),"implicit time refinement converges");
    std::size_t refinements=0;
    const auto refined=cool(.1,4,&refinements);
    require(refinements>0,"bounded nonlinear refinement exercised");
    require(std::abs(refined-exact)<std::abs(coarse-exact),"nonlinear-driven refinement converges too");
    // Temperature-varying pressure film is continuous to vacuum; no solid
    // conductance or radiation is pressure-scaled. Compare identical starts.
    NativeThermalSolver films(1,1,{},{{0,0,1,{}}},{});
    std::array hot{ThermalBody({100,400})};
    double previous=0;
    for(double pressure:{0.,1e-12,1e-6,.001,1.,100.,1e5}) {
        std::array inventory{NativeThermalGas{100,30000,pressure/300,{.21,.79,0,0},true}};
        require(films.solve(hot,inventory,{},.1),"film pressure sweep");audit(films);
        require(films.heatJ()[1]>=previous,"film transfer continuous pressure ordering");
        previous=films.heatJ()[1];
    }
    // Linear backward Euler has a known finite-capacity result, even when stiff.
    NativeThermalSolver stiff(2,0,{{0,1,1e7}},{},{});
    std::array pair{ThermalBody({100,400}),ThermalBody({200,200})};
    require(stiff.solve(pair,{},{},.1),"stiff passive solve");audit(stiff);
    const double transfer=.1*1e7*200/(1+.1*1e7*(1./100+1./200));
    near(stiff.heatJ()[1],transfer,1e-7,"stiff exact backward Euler");
    require(stiff.temperatureK()[0]>=stiff.temperatureK()[1],"implicit no overshoot");
    // Solar is already projected/absorbed power, not area times that power.
    NativeThermalSolver sun(1,0,{}, {},{{0,ThermalRadiationBoundary::Exterior,0,9,0,.5}});
    std::array solar{NativeThermalExterior{3,100}};
    require(sun.solve(hot,{},solar,.1),"projected solar");near(sun.heatJ()[0],10,1e-10,"projected solar counted once");
    NativeThermalBudget refuse;refuse.maxNonlinearIterations=0;
    require(!sun.solve(hot,{},solar,1,refuse),"nonlinear exhaustion refuses time");
    near(sun.heatJ()[0],10,1e-10,"nonlinear refusal retains output");
    // Real diffuse enclosures: analytic fields, reciprocity and implicit heat.
    constexpr double sigma=5.670374419e-8;
    std::vector<ThermalRadiationContact> faces{{0,ThermalRadiationBoundary::Cavity,0,10,.8,0},
        {1,ThermalRadiationBoundary::Cavity,1,5,.6,0}};
    DiffuseRadiation cavities(2,5,faces,{{0,1,2,3},{2,3,1,3},{4,std::nullopt,1,3},{0,0,10000,3}});
    std::array<long double,2> temperatures{400,200},diag{},source{},power{};
    require(cavities.freeze(temperatures,diag,source),"coupled cavity freeze");
    const double e0=sigma*std::pow(400.,4),e1=sigma*std::pow(200.,4);
    const double q=(e0-e1)/(1./8+1./2+1./3);
    near(*cavities.trialIrradiance(0),e0-q/8,1e-10,"hot cavity analytic irradiance");
    near(*cavities.trialIrradiance(1),e1+q/3,1e-10,"cold cavity analytic irradiance");
    near(cavities.trialPortalPowerW(0),q,1e-10,"reciprocal portal booked once");
    near(cavities.trialPortalPowerW(3),0,0,"same Sector portal no sink");
    require(!cavities.trialIrradiance(2) && !cavities.trialIrradiance(3),"unanchored connected fields unavailable");
    near(*cavities.trialIrradiance(4),sigma*81,1e-14,"boundary-only field available");
    cavities.apply(temperatures,power);
    near(source[0]-power[0],-q,1e-9,"surface to portal hot balance");
    near(source[1]-power[1],q,1e-9,"surface to portal cold balance");
    std::array<long double,2> x{1,0},y{0,1},lx{},ly{};
    cavities.apply(x,lx);cavities.apply(y,ly);
    near(lx[1],ly[0],1e-12,"frozen eliminated cavity operator symmetric");
    NativeThermalSolver coupled(2,0,{}, {},faces,5);
    require(coupled.solve(pair,{},{},.1,{},&cavities),"implicit coupled cavities");audit(coupled);
    require(coupled.heatJ()[0]<0 && coupled.heatJ()[1]>0,"radiation at every gas pressure including absent gas");
    NativeThermalBudget cavityLimit;cavityLimit.maxWork=1;
    const auto committed=coupled.heatJ()[0];
    require(!coupled.solve(pair,{},{},1,cavityLimit,&cavities),"nested cavity work budget refusal");
    near(coupled.heatJ()[0],committed,0,"cavity refusal retains publication");
    require(coupled.report().work==1,"nested work never exceeds whole-call budget");
    std::vector<ThermalRadiationContact> enclosure{{0,ThermalRadiationBoundary::Cavity,0,100,.8,0}};
    DiffuseRadiation aperture(1,1,enclosure,{{0,std::nullopt,1.8,3}});
    std::array<long double,1> room{300},rd{},rs{};
    require(aperture.freeze(room,rd,rs),"aperture freeze");
    const double limit=1.8*sigma*(std::pow(300.,4)-81);
    const auto loss=aperture.trialPortalPowerW(0);
    near(loss,limit*80/(80+1.8),1e-9,"simple grey enclosure analytic aperture loss");
    require(loss>0 && loss<limit,"black aperture bound uses clear area not full room area");
    NativeThermalSolver opening(1,0,{}, {},enclosure,1);
    std::array roomBody{ThermalBody({1000,300})};
    require(opening.solve(roomBody,{},{},.1,{},&aperture),"implicit aperture loss");audit(opening);
    DiffuseRadiation closed(1,1,enclosure,{{0,0,1e6,3}});
    require(opening.solve(roomBody,{},{},.1,{},&closed),"closed isothermal enclosure");
    near(opening.heatJ()[0],0,1e-10,"closed isothermal exchange zero");
    auto reflecting=enclosure;reflecting[0].emissivity=0;
    DiffuseRadiation mirror(1,1,reflecting,{});
    require(mirror.freeze(room,rd,rs) && !mirror.trialIrradiance(0),"reflecting faces neither absorb nor invent temperature");
    tracking=true;
    const auto cavitySuccess=coupled.solve(pair,{},{},1,{},&cavities);
    const auto apertureSuccess=opening.solve(roomBody,{},{},1,{},&aperture);
    tracking=false;
    require(cavitySuccess && apertureSuccess && allocations==0,"nested sparse radiation allocates nothing on success");
    // A longer sparse portal chain has the series radiative resistance of
    // its apertures; no dense surface pair expansion is needed.
    auto chainFaces=faces;chainFaces[1].cavity=11;
    std::vector<ThermalRadiationPortal> chainPortals;
    for(std::size_t i=0;i<11;++i) chainPortals.push_back({i,i+1,2,3});
    DiffuseRadiation chain(2,12,chainFaces,chainPortals);
    require(chain.freeze(temperatures,diag,source),"sparse chain fields");
    near(chain.trialPortalPowerW(5),(e0-e1)/(1./8+11./2+1./3),1e-9,"serial portal resistance");
    NativeThermalSolver chainSolver(2,0,{}, {},chainFaces,12);
    require(chainSolver.solve(pair,{},{},.1,{},&chain),"implicit sparse portal chain");audit(chainSolver);
    auto apertureCooling=[&](double interval) {
        DiffuseRadiation field(1,1,enclosure,{{0,std::nullopt,1.8,0}});
        NativeThermalSolver solver(1,0,{}, {},enclosure,1);
        std::array inventory{ThermalBody({100,1000})};
        NativeThermalBudget options;options.maxIntervalSeconds=interval;
        require(solver.solve(inventory,{},{},.1,options,&field),"aperture nonlinear refinement");audit(solver);
        return solver.temperatureK()[0];
    };
    const auto apertureExact=std::pow(std::pow(1000.,-3)+3*sigma*(80*1.8/(80+1.8))*.1/100,-1./3);
    require(std::abs(apertureCooling(.025)-apertureExact)<std::abs(apertureCooling(.1)-apertureExact),"grey enclosure time refinement converges");
    // Algebraic extension has no zero-capacity cavity node.
    NativeThermalSolver algebraic(2,0,{}, {},{{0,ThermalRadiationBoundary::Cavity,0,1,1,0}},1);
    PairOperator op;
    require(!algebraic.solve(bodies,{},{},.1),"no silent cavity omission");
    require(algebraic.solve(bodies,{},{},.1,{},&op),"algebraic operator solve");audit(algebraic);
    require(algebraic.heatJ()[0]<0 && algebraic.heatJ()[1]>0,"algebraic conservative exchange");
    gas[0]={100,30000,101325./300,{.21,.79,0,0},true};
    tracking=true;
    const bool success=algebraic.solve(bodies,{},{},1,{},&op);
    const bool nonlinearSuccess=vacuum.solve(bodies,gas,boundary,1);
    tracking=false;
    require(success && nonlinearSuccess && allocations==0,"successful linear/nonlinear solves allocate nothing");
    std::cout<<"native sparse nonlinear thermal fixtures passed; radiation errors "<<std::abs(coarse-exact)<<" -> "<<std::abs(fine-exact)<<" K\n";
}
