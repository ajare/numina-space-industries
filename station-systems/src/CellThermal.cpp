#include "station/CellThermal.h"
#include "StationHullThermal.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace station {
CellThermalSolver::CellThermalSolver(std::size_t n,std::vector<CellThermalLink> links)
    :links_(std::move(links)),diagonal_(n),rhs_(n),delta_(n),residual_(n),direction_(n),product_(n),z_(n),nodeFlux_(n),heat_(n),transferred_(links_.size()) {
    for (const auto& e:links_) if (e.a>=n || e.b>=n || e.a==e.b || !std::isfinite(e.conductanceWPerK) || e.conductanceWPerK<0)
        throw std::invalid_argument("Invalid Cell thermal link");
    if (!links_.empty()) for (const auto candidate:{links_[0].a,links_[0].b}) {
        bool common=true;for (const auto& e:links_) common=common && (e.a==candidate || e.b==candidate);
        if (common) {starHub_=candidate;break;}
    }
}
bool CellThermalSolver::solve(std::span<const long double> c,std::span<const long double> t,double dt,std::size_t limit) {
    diagnostic_.clear();iterations_=0;correction_=fluxResidual_=0;
    std::fill(heat_.begin(),heat_.end(),0);std::fill(transferred_.begin(),transferred_.end(),0);
    auto fail=[&](const char* why) {diagnostic_=why;std::fill(heat_.begin(),heat_.end(),0);
        std::fill(transferred_.begin(),transferred_.end(),0);return false;};
    const auto n=heat_.size();
    if (c.size()!=n || t.size()!=n || !std::isfinite(dt) || dt<0) return fail("Invalid Cell thermal solve input");
    for (std::size_t i=0;i<n;++i) if (!std::isfinite(c[i]) || c[i]<0 || !std::isfinite(t[i]) || t[i]<0)
        return fail("Nonphysical Cell thermal body");
    if (dt==0 || n==0) return true;
    std::fill(delta_.begin(),delta_.end(),0);
    if (starHub_!=SIZE_MAX) {
        // Exact elimination for a common Hull/reservoir star. This solves the
        // same backward-Euler matrix, including parallel edges, in two passes.
        // It is both cheaper and better conditioned than CG for trace leaves.
        const auto hub=starHub_;
        if (c[hub]>0) {
            std::fill(diagonal_.begin(),diagonal_.end(),0);
            for (const auto& e:links_) {
                const auto leaf=e.a==hub ? e.b : e.a;
                if (c[leaf]>0) diagonal_[leaf]+=static_cast<long double>(dt)*e.conductanceWPerK;
            }
            long double denominator=c[hub],numerator=0;bool changed=false;
            for (std::size_t i=0;i<n;++i) {
                z_[i]=0;if (i==hub || !(c[i]>0) || !(diagonal_[i]>0)) continue;
                const auto kd=diagonal_[i];
                const auto w=kd<=c[i] ? kd/(1+kd/c[i]) : c[i]/(1+c[i]/kd);
                z_[i]=w;denominator+=w;numerator+=w*(t[i]-t[hub]);changed=changed || t[i]!=t[hub];
            }
            if (!std::isfinite(denominator) || !std::isfinite(numerator)) return fail("Cell thermal star exceeds finite range");
            if (changed && limit==0) return fail("Cell thermal iteration work limit exceeded");
            delta_[hub]=numerator/denominator;
            for (std::size_t i=0;i<n;++i) if (i!=hub && c[i]>0 && z_[i]>0)
                delta_[i]=(z_[i]/c[i])*((t[hub]-t[i])+delta_[hub]);
            iterations_=changed ? 1 : 0;
        }
    } else {
        std::fill(rhs_.begin(),rhs_.end(),0);
        for (std::size_t i=0;i<n;++i) diagonal_[i]=c[i]>0 ? c[i] : 1;
        for (const auto& e:links_) if (c[e.a]>0 && c[e.b]>0) {
            const long double w=static_cast<long double>(dt)*e.conductanceWPerK;
            diagonal_[e.a]+=w;diagonal_[e.b]+=w;
            const auto q=w*(t[e.b]-t[e.a]);rhs_[e.a]+=q;rhs_[e.b]-=q;
        }
        long double norm=0;
        for (std::size_t i=0;i<n;++i) {
            residual_[i]=rhs_[i];direction_[i]=z_[i]=rhs_[i]/diagonal_[i];norm+=rhs_[i]*z_[i];
        }
        if (!std::isfinite(norm)) return fail("Cell thermal right-hand side exceeds finite range");
        const auto tolerance=std::max(1e-48L,norm*1e-28L);
        bool converged=norm<=tolerance;
        for (;!converged && iterations_<limit;++iterations_) {
            for (std::size_t i=0;i<n;++i) product_[i]=(c[i]>0 ? c[i] : 1)*direction_[i];
            for (const auto& e:links_) if (c[e.a]>0 && c[e.b]>0) {
                const auto q=static_cast<long double>(dt)*e.conductanceWPerK*(direction_[e.a]-direction_[e.b]);
                product_[e.a]+=q;product_[e.b]-=q;
            }
            long double denominator=0;for (std::size_t i=0;i<n;++i) denominator+=direction_[i]*product_[i];
            if (!(denominator>0) || !std::isfinite(denominator)) return fail("Cell thermal matrix lost positive definiteness");
            const auto alpha=norm/denominator;long double next=0;
            for (std::size_t i=0;i<n;++i) {
                delta_[i]+=alpha*direction_[i];residual_[i]-=alpha*product_[i];z_[i]=residual_[i]/diagonal_[i];next+=residual_[i]*z_[i];
            }
            if (!std::isfinite(next)) return fail("Nonfinite Cell thermal residual");
            converged=next<=tolerance;
            if (!converged) for (std::size_t i=0;i<n;++i) direction_[i]=z_[i]+(next/norm)*direction_[i];
            norm=next;
        }
        if (!converged) return fail("Cell thermal iteration work limit exceeded");
    }
    long double sum=0,magnitude=0,minimum=0,maximum=0;std::size_t anchor=0;
    bool hasBody=false;
    for (std::size_t i=0;i<n;++i) if (c[i]>0) {
        if (!hasBody) {minimum=maximum=t[i];hasBody=true;}
        minimum=std::min(minimum,t[i]);maximum=std::max(maximum,t[i]);
    }
    for (std::size_t i=0;i<n;++i) {
        if (!std::isfinite(delta_[i]) || t[i]+delta_[i]<0) return fail("Cell thermal solve crossed absolute zero");
        heat_[i]=static_cast<double>(c[i]*delta_[i]);
        if (!std::isfinite(heat_[i])) return fail("Cell thermal transfer exceeds finite range");
        sum+=heat_[i];magnitude+=std::abs(heat_[i]);if (c[i]>c[anchor]) anchor=i;
    }
    const auto budget=1e-7L+1e-12L*magnitude;
    if (std::abs(sum)>budget) return fail("Cell thermal energy convergence budget exceeded");
    correction_=static_cast<double>(-sum);heat_[anchor]+=correction_;
    for (std::size_t i=0;i<n;++i) if (c[i]>0) {
        const auto end=t[i]+heat_[i]/c[i];
        if (end<0 || end<minimum-1e-10L*std::max(1.0L,maximum) || end>maximum+1e-10L*std::max(1.0L,maximum))
            return fail("Cell thermal solve violated passive temperature bounds");
    }
    std::fill(nodeFlux_.begin(),nodeFlux_.end(),0);
    for (std::size_t i=0;i<links_.size();++i) {
        const auto& e=links_[i];if (!(c[e.a]>0 && c[e.b]>0)) continue;
        const auto q=static_cast<long double>(dt)*e.conductanceWPerK*((t[e.a]-t[e.b])+(delta_[e.a]-delta_[e.b]));
        transferred_[i]=static_cast<double>(q);
        if (!std::isfinite(transferred_[i])) return fail("Cell thermal link transfer exceeds finite range");
        nodeFlux_[e.a]-=q;nodeFlux_[e.b]+=q;
    }
    for (std::size_t i=0;i<n;++i) fluxResidual_=std::max(fluxResidual_,static_cast<double>(std::abs(nodeFlux_[i]-heat_[i])));
    if (!std::isfinite(fluxResidual_) || fluxResidual_>budget) return fail("Cell thermal flux convergence budget exceeded");
    return true;
}
std::optional<CellHullRadiation> integrateCellHullRadiation(const HullSettings& h,long double t,double absorbedKW,double dt) {
    if (!std::isfinite(t) || t<0 || !std::isfinite(dt) || dt<0 || !std::isfinite(absorbedKW) || absorbedKW<0
        || !(h.emissivity>0) || !(h.emittingAreaM2>0) || !(h.heatCapacityKJPerK>0)) return {};
    for (const double v:{h.emissivity,h.emittingAreaM2,h.heatCapacityKJPerK,h.sinkTemperatureC}) if (!std::isfinite(v)) return {};
    if (h.emissivity>1 || h.sinkTemperatureC < -StationCelsiusOffset) return {};
    const auto delta=hullRadiationTemperatureChangeC(h,t-StationCelsiusOffset,absorbedKW,dt);
    if (!delta) return {};
    const double absorbed=absorbedKW*1000*dt;
    const double emitted=static_cast<double>(absorbed-h.heatCapacityKJPerK*1000*(*delta));
    if (!std::isfinite(absorbed) || !std::isfinite(emitted) || t+*delta<0) return {};
    return CellHullRadiation{*delta,absorbed,emitted};
}
} // namespace station
