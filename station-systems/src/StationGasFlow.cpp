#include "StationGasFlow.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace station {
namespace {
constexpr std::array<double,4> molarMass{StationMolarMassOxygenKgPerMol,
    StationMolarMassNitrogenKgPerMol,StationMolarMassCarbonDioxideKgPerMol,StationMolarMassWaterKgPerMol};
double energy(const GasVolume& v) { return gasMixture(v.mass).capacityKJPerK*v.temperatureK; }
void transfer(GasVolume& from,GasVolume* to,const std::array<double,4>& mass) {
    double carried=0;
    for (std::size_t g=0;g<4;++g) carried+=mass[g]*StationGasCpKJPerKgK[g]*from.temperatureK;
    const double sourceEnergy=energy(from)-carried;
    const double destinationEnergy=to ? energy(*to)+carried : 0;
    for (std::size_t g=0;g<4;++g) {
        from.mass[g]-=mass[g];
        if (to) to->mass[g]+=mass[g];
    }
    const auto capacity=gasMixture(from.mass).capacityKJPerK;
    if (capacity>0) from.temperatureK=sourceEnergy/capacity;
    if (to) {
        const auto c=gasMixture(to->mass).capacityKJPerK;
        if (c>0) to->temperatureK=destinationEnergy/c;
    }
}
}
GasMixture gasMixture(const std::array<double,4>& mass) {
    GasMixture m;
    for (std::size_t g=0;g<4;++g) {
        m.massKg+=mass[g];
        m.cpKJPerK+=mass[g]*StationGasCpKJPerKgK[g];
        m.gasConstantKJPerK+=mass[g]*StationUniversalGasConstantJPerMolK/(1000*molarMass[g]);
    }
    m.capacityKJPerK=m.cpKJPerK-m.gasConstantKJPerK;
    return m;
}
double gasPressurePa(const GasVolume& v) {
    return 1000*gasMixture(v.mass).gasConstantKJPerK*v.temperatureK/v.volumeM3;
}
GasFlow compressibleGasFlow(const GasVolume* a,const GasVolume* b,double area,double discharge) {
    GasFlow result;
    const double pa=a ? gasPressurePa(*a) : 0,pb=b ? gasPressurePa(*b) : 0;
    const double upstream=std::max(pa,pb),downstream=std::min(pa,pb);
    result.pressureRatio=upstream>0 ? downstream/upstream : 1;
    if (!(area>0) || !(discharge>0)) { result.regime=StationAtmosphereFlowRegime::Sealed; return result; }
    // Below this continuum pressure, or within a relative equilibrium band,
    // no bulk transfer is resolvable. This does not fabricate vacuum gas.
    if (!(upstream>1e-6) || upstream-downstream<=upstream*1e-9) return result;
    const auto* donor=pa>pb ? a : b;
    const auto mixture=gasMixture(donor->mass);
    const double gamma=mixture.gamma();
    const double r=1000*mixture.gasConstantKJPerK/mixture.massKg;
    const double critical=std::pow(2/(gamma+1),gamma/(gamma-1));
    double shape;
    if (result.pressureRatio<=critical) {
        result.regime=StationAtmosphereFlowRegime::Choked;
        shape=std::sqrt(gamma)*std::pow(2/(gamma+1),(gamma+1)/(2*(gamma-1)));
    } else {
        result.regime=StationAtmosphereFlowRegime::Subsonic;
        const double logRatio=std::log(result.pressureRatio);
        shape=std::sqrt(2*gamma/(gamma-1)*std::exp(2/gamma*logRatio)
            * -std::expm1((gamma-1)/gamma*logRatio));
    }
    result.massKgPerS=std::copysign(discharge*area*upstream/std::sqrt(r*donor->temperatureK)*shape,pa-pb);
    return result;
}
std::vector<GasTransfer> integrateGasOpenings(std::vector<GasVolume>& volumes,
    const std::vector<GasOpening>& paths,double seconds) {
    if (!std::isfinite(seconds) || seconds<0) throw std::runtime_error("Invalid gas integration duration");
    std::vector<GasTransfer> results(paths.size());
    const bool active=std::any_of(paths.begin(),paths.end(),[](const auto& p){return p.areaM2>0 && p.dischargeCoefficient>0;});
    if (!active || seconds<=0) return results;
    auto at=[&](std::size_t i)->GasVolume* { return i==SIZE_MAX ? nullptr : &volumes.at(i); };
    double remaining=seconds;
    std::size_t steps=0;
    while (remaining>0) {
        if (++steps>2000000) throw std::runtime_error("Compressible gas integration exceeded substep budget");
        double dt=std::min(remaining,0.002);
        bool transporting=false;
        for (const auto& p:paths) {
            const auto flow=compressibleGasFlow(at(p.a),at(p.b),p.areaM2,p.dischargeCoefficient);
            if (!std::isfinite(flow.massKgPerS)) throw std::runtime_error("Non-finite compressible gas flow");
            if (flow.massKgPerS!=0) {
                transporting=true;
                const auto& donor=*at(flow.massKgPerS>0 ? p.a : p.b);
                dt=std::min(dt,0.01*gasMixture(donor.mass).massKg/std::abs(flow.massKgPerS));
            }
            if (p.a!=SIZE_MAX && p.b!=SIZE_MAX && p.areaM2>0 && p.dischargeCoefficient>0) {
                const auto& a=*at(p.a);const auto& b=*at(p.b);
                const double na=gasMixture(a.mass).gasConstantKJPerK,nb=gasMixture(b.mass).gasConstantKJPerK;
                if (na>0 && nb>0) for (std::size_t g=0;g<4;++g)
                    if (std::abs(a.mass[g]/molarMass[g]/na-b.mass[g]/molarMass[g]/nb)>1e-12) transporting=true;
            }
        }
        if (!transporting) break;
        if (!(dt>0) || !std::isfinite(dt) || remaining-dt==remaining) throw std::runtime_error("Invalid compressible gas substep");
        for (std::size_t index=0;index<paths.size();++index) {
            const auto& p=paths[index]; auto& out=results[index];
            auto* a=at(p.a); auto* b=at(p.b);
            const auto flow=compressibleGasFlow(a,b,p.areaM2,p.dischargeCoefficient);
            if (flow.massKgPerS!=0) {
                const double sign=flow.massKgPerS>0 ? 1 : -1;
                auto& source=*(sign>0 ? a : b); auto* destination=sign>0 ? b : a;
                const auto mixture=gasMixture(source.mass);
                double amount=std::min(std::abs(flow.massKgPerS)*dt,0.01*mixture.massKg);
                auto dose=[&](double mass) {
                    std::array<double,4> d{};
                    for (std::size_t g=0;g<4;++g) d[g]=source.mass[g]*(mass/mixture.massKg);
                    return d;
                };
                if (destination) {
                    auto crosses=[&](double mass) {
                        auto s=source,d=*destination; transfer(s,&d,dose(mass));
                        return gasPressurePa(s)<gasPressurePa(d);
                    };
                    if (crosses(amount)) {
                        double lo=0,hi=amount;
                        for (int iteration=0;iteration<48;++iteration) {
                            const double mid=(lo+hi)/2;
                            (crosses(mid) ? hi : lo)=mid;
                        }
                        amount=(lo+hi)/2;
                    }
                }
                const auto moved=dose(amount);
                double carried=0;
                for (std::size_t g=0;g<4;++g) {
                    out.massKg[g]+=sign*moved[g];
                    carried+=moved[g]*StationGasCpKJPerKgK[g]*source.temperatureK;
                }
                transfer(source,destination,moved);
                out.bulkMassKg+=sign*amount; out.energyKJ+=sign*carried;
            }
            // Composition mixing is molar counterdiffusion, not another bulk
            // pressure equalisation law. No mixing reservoir exists in vacuum.
            if (!a || !b || !(p.areaM2>0) || !(p.dischargeCoefficient>0)) continue;
            const auto ma=gasMixture(a->mass),mb=gasMixture(b->mass);
            const double na=ma.gasConstantKJPerK/(StationUniversalGasConstantJPerMolK/1000);
            const double nb=mb.gasConstantKJPerK/(StationUniversalGasConstantJPerMolK/1000);
            if (!(na>0) || !(nb>0)) continue;
            const double relaxed=-std::expm1(-StationPathwayDiffusionCoefficients[0]*p.areaM2*(a->temperatureK/a->volumeM3+b->temperatureK/b->volumeM3)*dt);
            const double equivalent=na*(nb/(na+nb))*relaxed;
            std::array<double,4> mixing{};
            double carried=0,scale=1;
            for (std::size_t g=0;g<4;++g) {
                mixing[g]=molarMass[g]*equivalent*(a->mass[g]/molarMass[g]/na-b->mass[g]/molarMass[g]/nb);
                carried+=mixing[g]*StationGasCpKJPerKgK[g]*(mixing[g]>0 ? a->temperatureK : b->temperatureK);
                const double available=mixing[g]>0 ? a->mass[g] : b->mass[g];
                if (mixing[g]!=0) scale=std::min(scale,0.01*available/std::abs(mixing[g]));
            }
            if (carried>0) scale=std::min(scale,0.01*energy(*a)/carried);
            if (carried<0) scale=std::min(scale,-0.01*energy(*b)/carried);
            const double ea=energy(*a)-carried*scale,eb=energy(*b)+carried*scale;
            for (std::size_t g=0;g<4;++g) {
                const double moved=mixing[g]*scale;
                a->mass[g]-=moved; b->mass[g]+=moved; out.massKg[g]+=moved;
            }
            a->temperatureK=ea/gasMixture(a->mass).capacityKJPerK;
            b->temperatureK=eb/gasMixture(b->mass).capacityKJPerK;
            out.energyKJ+=carried*scale;
        }
        remaining=dt>=remaining ? 0 : remaining-dt;
    }
    return results;
}
} // namespace station
