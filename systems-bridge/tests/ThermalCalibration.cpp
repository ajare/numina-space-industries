// #24 calibration study only. Not a production preset API or playback backend.
#include <nsi/systems/CellAtmosphere.h>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
constexpr double sigma=5.670374419e-8, R=8.314462618;
constexpr double continuum=20, accommodation=0.8, referencePressure=101325;
// O2, N2, CO2, H2O: rounded ideal-gas calibration constants, SI molar units.
constexpr std::array<double,4> mass{0.0319988,0.0280134,0.04401,0.01801528};
constexpr std::array<double,4> cv{20.8,20.8,28.8,25.3};
using Mixture=std::array<double,4>; // wet mole fractions, not mass fractions
constexpr Mixture air{0.21,0.79,0,0};
void require(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void near(double a,double b,double tolerance,const char* message) {require(std::abs(a-b)<=tolerance,message);}
double fourth(double t) {return t*t*t*t;}
// Symmetric Kennard-style effective accommodation; see docs/thermal-calibration.md.
double kinetic(double t,const Mixture& x,double alpha) {
    double k=0;
    for (std::size_t i=0;i<x.size();++i)
        k+=x[i]*(cv[i]+R/2)/std::sqrt(2*std::numbers::pi*mass[i]*R*t);
    return alpha/(2-alpha)*k;
}
double film(double p,double t,const Mixture& x=air,double alpha=accommodation) {
    const double molecular=kinetic(t,x,alpha)*p;
    // No division by pressure: vacuum and zero accommodation are exact zeros.
    return molecular/(1+molecular/continuum);
}
double contact(double gas,double surface) {
    return film(referencePressure,293.15)*(gas-surface)
        +0.8*sigma*(fourth(gas)-fourth(surface));
}
template<class F> double root(double lo,double hi,F residual) {
    require(residual(lo)<=0 && residual(hi)>=0,"Root is not bracketed");
    for (int i=0;i<100;++i) {const double mid=(lo+hi)/2;if (residual(mid)<0) lo=mid;else hi=mid;}
    return (lo+hi)/2;
}
struct Material {
    const char* name;
    double resistance, capacity, sideFraction, emissivity, absorptivity;
    // Sheet conductance k*d (W/K per square); contact per seam length (W/(m K)).
    double sheet, junction;
};
constexpr std::array<Material,3> hulls{{
    {"BareSkin",0.08422794320237184,400,0.5,0.8,0.4,0.02,0.1},
    {"StandardInsulated",1.6328624074472875,400,0.5,0.85,0.2,0.02,0.1},
    {"MultiLayerBlanket",19.89308783467953,400,0.5,0.03,0.15,0.02,0.1}}};
constexpr std::array<Material,3> partitions{{
    {"BarePartition",0.1186077606265792,2000,0.5,0.8,0.4,0.1,0.1},
    {"ThermalBreak",0.9186101039856908,2000,0.5,0.8,0.4,0.1,0.1},
    {"InsulatedPartition",3.2519439103423147,2000,0.5,0.8,0.4,0.1,0.1}}};
void checkMaterials() {
    for (const auto& table : {hulls,partitions}) for (const auto& m:table) {
        for (double value:{m.resistance,m.capacity,m.sideFraction,m.emissivity,m.absorptivity,m.sheet,m.junction})
            require(std::isfinite(value),"Non-finite material");
        require(m.resistance>0 && m.capacity>0 && m.sheet>0 && m.junction>0,"Non-passive material");
        require(m.emissivity>0 && m.emissivity<=1 && m.absorptivity>0 && m.absorptivity<=1,"Invalid optical property");
        require(m.sideFraction>0 && m.sideFraction<1,"Invalid side allocation");
        near(m.capacity*m.sideFraction+m.capacity*(1-m.sideFraction),m.capacity,1e-12,"Capacity duplicated");
        for (double area:{6.,12.,18.}) {
            const double leaf=1.8*m.capacity, frame=(area-1.8)*m.capacity;
            near(leaf+frame,area*m.capacity,1e-10,"Door capacity budget changed");
            near((leaf+frame)*293.15,area*m.capacity*293.15,1e-8,"Door initial energy duplicated");
        }
    }
    using I=nsi::core::Installation;
    I installation(1,1);installation.setHullType(I::HullType::BareSkin);
    const auto expansion=nsi::systems::expandCellAtmosphere(installation);
    const auto& old=*expansion.thermal.hull;
    for (const auto& m:hulls)
        near(m.capacity,1000*old.heatCapacityKJPerK/old.emittingAreaM2,1e-10,"Exterior capacity anchor changed");
}
void anchors() {
    using I=nsi::core::Installation;
    constexpr std::array types{I::HullType::BareSkin,I::HullType::StandardInsulated,I::HullType::MultiLayerBlanket};
    constexpr std::array targets{224.0,62.0,3.8};
    for (std::size_t i=0;i<hulls.size();++i) {
        const auto& m=hulls[i];
        // Solve outward flux independently with frozen solid R; do not fit R here.
        const double q=root(0,targets[i]*2,[&](double q) {
            const double outer=std::pow(q/(m.emissivity*sigma)+fourth(3),0.25);
            return q-contact(293.15,outer+q*m.resistance);
        });
        const auto legacy=I::hullSpec(types[i]);
        near(legacy.calibratedFluxWPerM2,targets[i],1e-10,"Legacy Hull anchor changed");
        near(q,targets[i],0.05*targets[i],"Hull calibration missed 5% target");
        std::cout<<m.name<<" R="<<m.resistance<<" flux="<<q<<" W/m2\n";
    }
    constexpr std::array u{5.0,1.0,0.3};
    constexpr std::array partitionTypes{I::PartitionType::BarePartition,I::PartitionType::ThermalBreak,I::PartitionType::InsulatedPartition};
    for (std::size_t i=0;i<partitions.size();++i) {
        const auto& m=partitions[i];
        near(I::partitionSpec(partitionTypes[i]).transmittanceWPerM2K,u[i],1e-12,"Legacy Partition anchor changed");
        const double q=root(0,20/m.resistance,[&](double q) {
            const double hot=root(283.15,303.15,[&](double t){return q-contact(303.15,t);});
            const double cold=root(283.15,303.15,[&](double t){return -q-contact(283.15,t);});
            return q-(hot-cold)/m.resistance;
        });
        near(q/20,u[i],0.05*u[i],"Partition calibration missed 5% target");
        std::cout<<m.name<<" R="<<m.resistance<<" U="<<q/20<<" W/(m2 K)\n";
    }
}
void sweeps() {
    constexpr std::array<Mixture,7> mixtures{{air,{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1},
        {0.2,0.7,0.05,0.05},{0.1,0.1,0.4,0.4}}};
    std::size_t samples=0;
    for (auto x:mixtures) for (double t:{100.,200.,293.15,600.,1000.}) for (double alpha:{0.,0.1,0.8,1.}) {
        near(film(0,t,x,alpha),0,0,"Contact does not vanish at vacuum");
        double previous=0;
        for (int j=0;j<=600;++j) {
            const double p=std::pow(10.,-16+j/25.); // 1e-16 through 1e8 Pa
            const double h=film(p,t,x,alpha);
            require(std::isfinite(h) && h>=0 && h<=continuum,"Invalid gas contact");
            require(h>=previous,"Flux magnitude grows when pressure falls");
            // Fixed signed temperature differences, unit area. Pressure alters only film.
            for (double dt:{-100.,-1.,1.,100.}) require(std::abs(h*dt)>=std::abs(previous*dt),"Non-passive flux sweep");
            const double dh=film(p*(1+1e-6),t,x,alpha)-h;
            require(dh>=0 && dh<=continuum*1e-6,"Discontinuous contact");
            if (alpha>0) {
                const double k=kinetic(t,x,alpha);
                const double expected=continuum*continuum*k/std::pow(continuum+k*p,2);
                near(dh/(p*1e-6),expected,expected*2e-5+1e-8,"Pressure derivative mismatch");
            }
            previous=h;++samples;
        }
        if (alpha>0) {
            const double k=kinetic(t,x,alpha);
            near(film(continuum/k,t,x,alpha),continuum/2,1e-12,"Derived crossover mismatch");
            near(film(1e-16,t,x,alpha)/1e-16,k,k*1e-12,"Wrong molecular asymptote");
        }
    }
    std::cout<<"Pressure/mixture/temperature/accommodation samples="<<samples
        <<"; K_air="<<kinetic(293.15,air,accommodation)
        <<" W/(m2 K Pa); crossover="<<continuum/kinetic(293.15,air,accommodation)<<" Pa\n";
}
}
int main() {
    try {std::cout<<std::setprecision(12);checkMaterials();anchors();sweeps();}
    catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
