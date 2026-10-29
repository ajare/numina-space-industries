#include <station/NativeThermal.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <stdexcept>

namespace station {
void NativeThermalSolver::setGasContact(std::size_t index,std::size_t gas,double area) {
    if(gas>=gases_ || !std::isfinite(area) || area<0) throw std::invalid_argument("Invalid actuated gas contact");
    auto& c=gas_.at(index);c.gasCell=gas;c.areaM2=area;
}
void NativeThermalSolver::setRadiationArea(std::size_t index,double area) {
    if(!std::isfinite(area) || area<0) throw std::invalid_argument("Invalid actuated radiation area");
    radiation_.at(index).areaM2=area;
}
namespace {
bool valid(long double x) { return std::isfinite(x) && x>=0; }
long double dot(std::span<const long double> a,std::span<const long double> b) {
    long double s=0; for(std::size_t i=0;i<a.size();++i) s+=a[i]*b[i]; return s;
}
constexpr long double sigma=5.670374419e-8L;
}
NativeThermalSolver::NativeThermalSolver(std::size_t bodies,std::size_t gases,
    std::vector<ThermalSolidContact> solid,std::vector<ThermalGasContact> gas,
    std::vector<ThermalRadiationContact> radiation,std::size_t cavities)
    : bodies_(bodies),gases_(gases),solid_(std::move(solid)),gas_(std::move(gas)),radiation_(std::move(radiation)) {
    validateThermalContacts(bodies,gases,cavities,solid_,gas_,radiation_);
    if(gases>SIZE_MAX-bodies) throw std::length_error("Native thermal node count overflow");
    const auto n=bodies+gases;
    for(auto* v:{&capacity_,&current_,&start_,&guess_,&diagonal_,&source_,&opDiagonal_,&opSource_,
        &scratch_,&rhs_,&delta_,&residual_,&direction_,&product_,&z_,&power_,&totalHeat_}) v->resize(n);
    if(gas_.size()>SIZE_MAX-solid_.size()) throw std::length_error("Native thermal contact count overflow");
    conductance_.resize(solid_.size()+gas_.size()); radiationG_.resize(radiation_.size());
    heat_.resize(n); temperature_.resize(n);
}
bool NativeThermalSolver::tick() {
    if(report_.work>=budget_.maxWork) {diagnostic_="thermal whole-call work budget exhausted";return false;}
    ++report_.work; return true;
}
bool NativeThermalSolver::freeze(ThermalAlgebraicOperator* op) {
    if(!tick()) return false;
    std::fill(diagonal_.begin(),diagonal_.end(),0); std::fill(source_.begin(),source_.end(),0);
    if(op) {
        std::fill(opDiagonal_.begin(),opDiagonal_.end(),0); std::fill(opSource_.begin(),opSource_.end(),0);
        if(!op->freeze(guess_,opDiagonal_,opSource_)) {
            diagnostic_="thermal algebraic field solve/work budget exhausted";return false;
        }
        for(std::size_t i=0;i<capacity_.size();++i) {
            if(!valid(opDiagonal_[i]) || !std::isfinite(opSource_[i]) ||
                (capacity_[i]==0 && (opDiagonal_[i]!=0 || opSource_[i]!=0))) return false;
            diagonal_[i]=opDiagonal_[i]; source_[i]=opSource_[i];
        }
    }
    auto edge=[&](std::size_t a,std::size_t b,long double g,std::size_t k) {
        conductance_[k]=g; diagonal_[a]+=g; diagonal_[b]+=g;
    };
    std::size_t k=0;
    for(const auto& c:solid_) { edge(c.a,c.b,c.conductanceWPerK,k); ++k; }
    for(const auto& c:gas_) {
        const auto i=bodies_+c.gasCell;
        long double g=0;
        if(inventory_[c.gasCell].resolved && capacity_[i]>0) {
            const auto mean=guess_[i]/2+guess_[c.body]/2;
            const double pressure=static_cast<double>(inventory_[c.gasCell].pressurePaPerK*guess_[i]);
            if(mean<=0 || !std::isfinite(static_cast<double>(mean)) || !valid(pressure)) return false;
            g=c.areaM2*thermalGasFilmWPerM2K(c.film,
                pressure,
                static_cast<double>(mean),inventory_[c.gasCell].wetMoleFractions);
        }
        edge(c.body,i,g,k); ++k;
    }
    std::size_t e=0;
    for(std::size_t j=0;j<radiation_.size();++j) {
        const auto& c=radiation_[j]; radiationG_[j]=0;
        if(c.boundary!=ThermalRadiationBoundary::Exterior) continue;
        const auto& b=exterior_[e++];
        const long double t=guess_[c.body],s=b.spaceTemperatureK;
        // Exact nonnegative secant of T^4 about the exterior field.
        const auto g=c.emissivity*c.areaM2*sigma*(t+s)*(t*t+s*s);
        radiationG_[j]=g; diagonal_[c.body]+=g;
        source_[c.body]+=g*s+b.absorbedSolarW;
    }
    for(std::size_t i=0;i<capacity_.size();++i)
        if(!valid(diagonal_[i]) || !std::isfinite(source_[i])) return false;
    return true;
}
bool NativeThermalSolver::apply(std::span<const long double> x,std::span<long double> out,ThermalAlgebraicOperator* op) {
    std::fill(out.begin(),out.end(),0);
    if(op) { op->apply(x,out); if(!op->healthy()) {
        diagnostic_="thermal algebraic field solve/work budget exhausted";return false;
    } }
    std::size_t k=0;
    auto edge=[&](std::size_t a,std::size_t b) {
        const auto q=conductance_[k++]*(x[a]-x[b]); out[a]+=q; out[b]-=q;
    };
    for(const auto& c:solid_) edge(c.a,c.b);
    for(const auto& c:gas_) edge(c.body,bodies_+c.gasCell);
    for(std::size_t j=0;j<radiation_.size();++j) out[radiation_[j].body]+=radiationG_[j]*x[radiation_[j].body];
    return true;
}
bool NativeThermalSolver::interval(double dt,ThermalAlgebraicOperator* op) {
    start_=current_; guess_=start_;
    for(std::size_t outer=0;outer<budget_.maxNonlinearIterations;++outer) {
        ++report_.nonlinearIterations;
        if(!freeze(op)) return false;
        if(!apply(start_,scratch_,op)) return false;
        for(std::size_t i=0;i<capacity_.size();++i) {
            rhs_[i]=dt*(source_[i]-scratch_[i]); delta_[i]=0;
            residual_[i]=rhs_[i];
            const auto d=capacity_[i]+dt*diagonal_[i];
            z_[i]=d>0?residual_[i]/d:0; direction_[i]=z_[i];
        }
        auto rz=dot(residual_,z_);
        bool converged=false;
        for(std::size_t it=0;it<=budget_.maxLinearIterations;++it) {
            long double err=0,scale=0;
            for(std::size_t i=0;i<capacity_.size();++i) {err=std::max(err,std::abs(residual_[i]));scale+=std::abs(rhs_[i]);}
            if(err<=1e-11L+1e-18L*scale) {converged=true;break;}
            if(it==budget_.maxLinearIterations || !tick()) return false;
            ++report_.linearIterations;
            if(!apply(direction_,product_,op)) return false;
            for(std::size_t i=0;i<capacity_.size();++i) product_[i]=capacity_[i]*direction_[i]+dt*product_[i];
            const auto denom=dot(direction_,product_);
            if(!(denom>0) || !std::isfinite(denom)) return false;
            const auto alpha=rz/denom;
            for(std::size_t i=0;i<capacity_.size();++i) {
                delta_[i]+=alpha*direction_[i]; residual_[i]-=alpha*product_[i];
                const auto d=capacity_[i]+dt*diagonal_[i]; z_[i]=d>0?residual_[i]/d:0;
            }
            const auto next=dot(residual_,z_);
            for(std::size_t i=0;i<capacity_.size();++i) direction_[i]=z_[i]+next/rz*direction_[i];
            rz=next;
        }
        if(!converged) return false;
        long double linearResidual=0;
        for(std::size_t i=0;i<capacity_.size();++i) {
            guess_[i]=start_[i]+delta_[i];
            if(!valid(guess_[i])) return false;
            linearResidual=std::max(linearResidual,std::abs(residual_[i]));
        }
        if(!freeze(op)) return false;
        if(!apply(guess_,scratch_,op)) return false;
        long double error=0,scale=0;
        for(std::size_t i=0;i<capacity_.size();++i) {
            power_[i]=source_[i]-scratch_[i];
            if(!std::isfinite(power_[i]) || !std::isfinite(delta_[i])) return false;
            const auto heat=dt*power_[i]; scale+=std::abs(heat);
            error=std::max(error,std::abs(capacity_[i]*delta_[i]-heat));
        }
        report_.lastTrialNonlinearResidualJ=static_cast<double>(error);
        if(std::isfinite(error) && error<=1e-8L+1e-13L*scale) {
            long double boundary=0;
            if(op) {
                op->apply(guess_,scratch_);
                if(!op->healthy()) return false;
                if(const auto p=op->boundaryPowerW()) boundary+=dt*(*p);
                else for(std::size_t i=0;i<capacity_.size();++i) boundary+=dt*(opSource_[i]-scratch_[i]);
            }
            NativeThermalPaths paths;
            std::size_t k=0;
            for(const auto& c:solid_) {
                paths.solidConductionMagnitudeJ+=dt*std::abs(conductance_[k++]*(guess_[c.a]-guess_[c.b]));
            }
            for(const auto& c:gas_) {
                paths.gasIntoSolidJ+=dt*conductance_[k++]*(guess_[bodies_+c.gasCell]-guess_[c.body]);
            }
            if(op) {
                paths.interiorRadiationMagnitudeJ=dt*op->interiorRadiationMagnitudeW();
                paths.portalEscapeJ=-dt*op->boundaryPowerW().value_or(0);
            }
            std::size_t e=0;
            for(std::size_t j=0;j<radiation_.size();++j) {
                const auto& r=radiation_[j];
                if(r.boundary!=ThermalRadiationBoundary::Exterior) continue;
                const auto& b=exterior_[e++];
                boundary+=dt*(b.absorbedSolarW+radiationG_[j]*(b.spaceTemperatureK-guess_[r.body]));
                paths.absorbedSolarJ+=dt*b.absorbedSolarW;
                paths.exteriorRadiationJ+=dt*radiationG_[j]*(guess_[r.body]-b.spaceTemperatureK);
            }
            for(std::size_t i=0;i<capacity_.size();++i) {
                const auto heat=dt*power_[i];
                current_[i]=capacity_[i]>0?start_[i]+heat/capacity_[i]:start_[i];
                if(!valid(current_[i])) return false;
            }
            for(std::size_t i=0;i<capacity_.size();++i) totalHeat_[i]+=dt*power_[i];
            report_.paths+=paths;
            if(op) report_.cavityClosureW=std::max(report_.cavityClosureW,op->cavityClosureW());
            boundaryHeat_+=boundary;
            report_.exteriorHeatJ=static_cast<double>(boundaryHeat_);
            report_.nonlinearResidualJ=std::max(report_.nonlinearResidualJ,static_cast<double>(error));
            report_.fluxResidualJ=std::max(report_.fluxResidualJ,static_cast<double>(linearResidual));
            ++report_.intervals; return true;
        }
    }
    return false;
}
bool NativeThermalSolver::solve(std::span<const ThermalBody> bodies,std::span<const NativeThermalGas> gases,
    std::span<const NativeThermalExterior> exterior,double seconds,NativeThermalBudget budget,ThermalAlgebraicOperator* op) {
    report_={}; boundaryHeat_=0; diagnostic_="invalid thermal input"; budget_=budget;
    if(op) op->setWorkBudget(&report_.work,budget.maxWork);
    struct DetachWork {
        ThermalAlgebraicOperator* op;
        ~DetachWork() { if(op) op->setWorkBudget(nullptr,0); }
    } detach{op};
    if(bodies.size()!=bodies_ || gases.size()!=gases_ || !valid(seconds) || seconds<=0 ||
        !valid(budget.maxIntervalSeconds) || budget.maxIntervalSeconds<=0 || budget.maxIntervalSeconds>0.1) return false;
    std::size_t count=0;
    for(const auto& r:radiation_) {
        if(r.boundary==ThermalRadiationBoundary::Exterior) ++count;
        else if(!op) {diagnostic_="cavity contacts require an algebraic operator";return false;}
    }
    if(exterior.size()!=count) return false;
    for(const auto& e:exterior) if(!valid(e.spaceTemperatureK) || !valid(e.absorbedSolarW)) return false;
    for(std::size_t i=0;i<bodies_;++i) {capacity_[i]=bodies[i].capacityJPerK();current_[i]=bodies[i].temperatureK();}
    for(std::size_t j=0;j<gases_;++j) {
        const auto& g=gases[j];
        if(!valid(g.capacityJPerK) || !valid(g.energyJ) || !valid(g.pressurePaPerK) ||
            (g.resolved && (g.capacityJPerK<=0 || g.energyJ<=0))) return false;
        // Validate even detached inventory, but allow absent gas fractions.
        if(g.resolved) {
            try {thermalGasFilmWPerM2K({},0,300,g.wetMoleFractions);} catch(const std::invalid_argument&) {return false;}
        }
        capacity_[bodies_+j]=g.resolved?g.capacityJPerK:0;
        current_[bodies_+j]=g.capacityJPerK>0?g.energyJ/g.capacityJPerK:0;
        if(!valid(current_[bodies_+j])) return false;
    }
    inventory_=gases; exterior_=exterior;
    std::fill(totalHeat_.begin(),totalHeat_.end(),0);
    diagnostic_="thermal nonlinear/linear refinement exhausted";
    double remaining=seconds;
    while(remaining>0) {
        double dt=std::min(remaining,budget.maxIntervalSeconds);
        std::size_t refinement=0;
        while(!interval(dt,op)) {
            current_=start_;
            if(report_.work>=budget.maxWork || refinement++>=budget.maxRefinements) return false;
            ++report_.refinements; dt/=2;
            if(dt<=0 || remaining-dt==remaining) return false;
        }
        if(remaining-dt==remaining) return false;
        remaining-=dt;
    }
    long double sum=0,scale=0;
    for(std::size_t i=0;i<heat_.size();++i) {
        if(!valid(current_[i]) || !std::isfinite(static_cast<double>(current_[i])) ||
            !std::isfinite(static_cast<double>(totalHeat_[i]))) return false;
        sum+=static_cast<double>(totalHeat_[i]); scale+=std::abs(totalHeat_[i]);
    }
    report_.energyResidualJ=static_cast<double>(sum-boundaryHeat_);
    if(!std::isfinite(report_.exteriorHeatJ) || !std::isfinite(report_.energyResidualJ)) {
        diagnostic_="thermal boundary ledger overflow"; return false;
    }
    if(std::abs(report_.energyResidualJ)>1e-7L+1e-12L*scale) {diagnostic_="thermal energy residual exceeded";return false;}
    for(std::size_t i=0;i<heat_.size();++i) {heat_[i]=static_cast<double>(totalHeat_[i]);temperature_[i]=static_cast<double>(current_[i]);}
    diagnostic_={}; return true;
}
} // namespace station
