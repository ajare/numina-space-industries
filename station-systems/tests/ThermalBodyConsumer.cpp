#include <station/ThermalBody.h>
#include <station/CellFluid.h>
#include <station/CellThermal.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>
using namespace station;
namespace {
void require(bool x) { if (!x) throw std::runtime_error("Thermal body/contact check failed"); }
void near(double a,double b,double tol=1e-11) { require(std::abs(a-b)<=tol*std::max({1.,std::abs(a),std::abs(b)})); }
template<class F> void rejects(F f) {
    try { f(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid thermal body/contact accepted");
}
void bodies() {
    require(NativeThermalModelRevision=="local-structure-v2-cavity-candidate");
    ThermalBody body({200});near(body.temperatureK(),293.15);near(body.energyJ(),200*293.15);
    const auto restored=ThermalBody::fromCarriedEnergy(200,body.energyJ());near(restored.temperatureK(),body.temperatureK());
    body.addHeat(200);near(body.temperatureK(),294.15);
    const auto before=body.energyJ();rejects([&] { body.addHeat(-before-1); });near(body.energyJ(),before);
    rejects([&] { body.addHeat(std::numeric_limits<double>::infinity()); });near(body.energyJ(),before);
    for (double x:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { ThermalBody b({x}); });
    rejects([] { ThermalBody b({200,-1}); });
    rejects([] { ThermalBody b({1e308,1e308}); });
    rejects([] { ThermalBody::fromCarriedEnergy(200,-1); });
    rejects([] { ThermalBody::fromCarriedEnergy(1e-300,1e300); });
    rejects([] { ThermalBody b({1e-300,0});b.addHeat(1e300); });
    const auto gas=makeCellFluidState({0.21,0.79,0.,0.},293.15);
    const auto p=cellFluidPrimitive(gas,12);
    ThermalBody same({2000,p.temperatureK});
    // Body addition carries C*T as inventory. It neither rewrites the gas nor
    // changes its EOS capacity/pressure. At equilibrium every heat is zero.
    near((gas.q[6]+same.energyJ())/(p.capacityJPerK+same.capacityJPerK()),p.temperatureK);
    CellThermalSolver uniform(2,{{0,1,20}});
    require(uniform.solve(std::vector<long double>{p.capacityJPerK,same.capacityJPerK()},
        std::vector<long double>{p.temperatureK,same.temperatureK()},0.1));
    for (auto q:uniform.heatJ()) near(q,0);
    near(cellFluidPrimitive(gas,12).capacityJPerK,p.capacityJPerK);
    near(cellFluidPrimitive(gas,12).pressurePa,p.pressurePa);
    // Reuse the existing solver only as a consumer of separate capacities.
    ThermalBody hot({1000,400}),cold({2000,200});
    auto fluid=makeCellFluidState({0.21,0.79,0.,0.},300);
    const auto initialGas=cellFluidPrimitive(fluid,12);
    const double total=fluid.q[6]+hot.energyJ()+cold.energyJ();
    const double expected=total/(initialGas.capacityJPerK+3000);
    CellThermalSolver solver(3,{{0,1,100},{1,2,100}});
    for (int i=0;i<5000;++i) {
        auto g=cellFluidPrimitive(fluid,12);
        require(solver.solve(std::vector<long double>{g.capacityJPerK,1000,2000},
            std::vector<long double>{g.temperatureK,hot.temperatureK(),cold.temperatureK()},0.1));
        fluid.q[6]+=solver.heatJ()[0];hot.addHeat(solver.heatJ()[1]);cold.addHeat(solver.heatJ()[2]);
    }
    near(fluid.q[6]+hot.energyJ()+cold.energyJ(),total);
    near(hot.temperatureK(),expected,1e-8);near(cold.temperatureK(),expected,1e-8);
    near(cellFluidPrimitive(fluid,12).temperatureK,expected,1e-8);
    near(cellFluidPrimitive(fluid,12).capacityJPerK,initialGas.capacityJPerK);
}
void films() {
    constexpr std::array<double,4> air{0.21,0.79,0,0};
    ThermalGasFilmSettings f;
    for (auto mixture: {air,std::array<double,4>{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1},{0.1,0.1,0.4,0.4}})
        for (double t:{100.,293.15,1000.}) for (double a:{0.,0.1,0.8,1.}) {
            f.accommodation=a;near(thermalGasFilmWPerM2K(f,0,t,mixture),0);
            double previous=0;
            for (int i=0;i<=600;++i) {
                const double p=std::pow(10.,-16+i/25.);
                const double h=thermalGasFilmWPerM2K(f,p,t,mixture);
                require(std::isfinite(h) && h>=previous && h<=20);previous=h;
                const double next=thermalGasFilmWPerM2K(f,p*(1+1e-6),t,mixture);
                require(next>=h && next-h<=20e-6);
            }
        }
    f={20,0.8};
    // Independently derive molar cv from the public gas state/EOS for every
    // species. This prevents drift back to the study's separate rounded table.
    constexpr std::array masses{StationMolarMassOxygenKgPerMol,StationMolarMassNitrogenKgPerMol,
        StationMolarMassCarbonDioxideKgPerMol,StationMolarMassWaterKgPerMol};
    constexpr std::array<double,4> wet{0.2,0.6,0.05,0.15};
    double k=0;
    for (std::size_t i=0;i<4;++i) {
        std::array<double,4> inventory{};inventory[i]=masses[i];
        const auto p=cellFluidPrimitive(makeCellFluidState(inventory,293.15),12);
        k+=wet[i]*(p.capacityJPerK+StationUniversalGasConstantJPerMolK/2)
            /std::sqrt(2*std::numbers::pi*masses[i]*StationUniversalGasConstantJPerMolK*293.15);
    }
    k*=0.8/1.2;
    for (double pressure:{1e-12,1.,25.,101325.})
        near(thermalGasFilmWPerM2K(f,pressure,293.15,wet),k*pressure/(1+k*pressure/20));
    near(thermalGasFilmWPerM2K(f,1e308,293.15,air),20);
    near(thermalGasFilmWPerM2K({0,0.8},1e5,293.15,air),0);
    near(thermalGasFilmWPerM2K(f,1e-12,293.15,air)/1e-12,0.792431864619,0.003); // reconciled cv, <0.3% C1 change
    rejects([&] { thermalGasFilmWPerM2K(f,-1,293.15,air); });
    rejects([&] { thermalGasFilmWPerM2K(f,1,0,air); });
    rejects([&] { thermalGasFilmWPerM2K({20,1.1},1,293.15,air); });
    rejects([&] { thermalGasFilmWPerM2K(f,1,293.15,{0.5,0,0,0}); });
    rejects([&] { thermalGasFilmWPerM2K(f,1,293.15,{-0.1,1.1,0,0}); });
    rejects([&] { thermalGasFilmWPerM2K(f,std::numeric_limits<double>::quiet_NaN(),293.15,air); });
}
void contacts() {
    std::vector<ThermalSolidContact> s{{0,1,10}};
    std::vector<ThermalGasContact> g{{0,0,6,{20,0.8}}};
    std::vector<ThermalRadiationContact> r{{0,ThermalRadiationBoundary::Cavity,0,6,0.8,0.4},
        {1,ThermalRadiationBoundary::Exterior,999,6,0.85,0.2}};
    auto validate=[&] { validateThermalContacts(2,1,1,s,g,r); };validate();
    s[0].b=0;rejects(validate);s[0].b=2;rejects(validate);s[0].b=1;
    s[0].conductanceWPerK=-1;rejects(validate);s[0].conductanceWPerK=0;validate();
    g[0].gasCell=1;rejects(validate);g[0].gasCell=0;
    g[0].areaM2=-1;rejects(validate);g[0].areaM2=6;
    g[0].film.accommodation=2;rejects(validate);g[0].film.accommodation=0.8;
    r[0].cavity=1;rejects(validate);r[0].cavity=0;
    r[0].emissivity=1.1;rejects(validate);r[0].emissivity=0;
    r[0].boundary=static_cast<ThermalRadiationBoundary>(99);rejects(validate);
    r[0].boundary=ThermalRadiationBoundary::Cavity;
    validateThermalContacts(0,0,0,{},{},{});
}
}
int main() { try { bodies();films();contacts(); } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
