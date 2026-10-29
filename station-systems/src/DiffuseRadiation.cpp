#include <station/DiffuseRadiation.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace station {
namespace {
constexpr long double sigma=5.670374419e-8L;
long double dot(std::span<const long double> a,std::span<const long double> b) {
    long double s=0;for(std::size_t i=0;i<a.size();++i) s+=a[i]*b[i];return s;
}
bool valid(double x) {return std::isfinite(x) && x>=0;}
}
DiffuseRadiation::DiffuseRadiation(std::size_t nodes,std::size_t cavities,
    std::vector<ThermalRadiationContact> surfaces,std::vector<ThermalRadiationPortal> portals)
    :nodes_(nodes),surfaces_(std::move(surfaces)),portals_(std::move(portals)),
    diagonal_(cavities),boundary_(cavities),offset_(surfaces_.size()),active_(cavities),anchored_(cavities),parent_(cavities),
    rhs_(cavities),field_(cavities),residual_(cavities),direction_(cavities),product_(cavities),z_(cavities),trial_(cavities) {
    validateThermalContacts(nodes,0,cavities,{},{},surfaces_);
    for(const auto& s:surfaces_) if(s.boundary!=ThermalRadiationBoundary::Cavity)
        throw std::invalid_argument("Diffuse surface must own a cavity");
    updateCoefficients();
}
void DiffuseRadiation::setSurface(std::size_t index,std::size_t cavity,double area) {
    if(cavity>=diagonal_.size() || !valid(area)) throw std::invalid_argument("Invalid actuated cavity surface");
    auto& s=surfaces_.at(index);s.cavity=cavity;s.areaM2=area;
}
void DiffuseRadiation::setPortalArea(std::size_t index,double area) {
    if(!valid(area)) throw std::invalid_argument("Invalid actuated portal area");
    portals_.at(index).areaM2=area;
}
void DiffuseRadiation::updateCoefficients() {
    const auto cavities=diagonal_.size();
    trialValid_=false;
    std::fill(diagonal_.begin(),diagonal_.end(),0);std::fill(boundary_.begin(),boundary_.end(),0);
    std::fill(active_.begin(),active_.end(),false);std::fill(anchored_.begin(),anchored_.end(),false);
    auto& parent=parent_;std::iota(parent.begin(),parent.end(),0);
    auto root=[&](std::size_t i) {while(parent[i]!=i) {parent[i]=parent[parent[i]];i=parent[i];}return i;};
    for(const auto& p:portals_) {
        if(p.a>=cavities || (p.b && *p.b>=cavities) || !valid(p.areaM2) || !valid(p.spaceTemperatureK))
            throw std::invalid_argument("Invalid radiation portal");
        if(p.b && *p.b==p.a) continue;
        diagonal_[p.a]+=p.areaM2;
        if(p.b) {diagonal_[*p.b]+=p.areaM2;if(p.areaM2>0) parent[root(p.a)]=root(*p.b);}
        else {boundary_[p.a]+=p.areaM2*sigma*std::pow(static_cast<long double>(p.spaceTemperatureK),4);if(p.areaM2>0) active_[p.a]=true;}
    }
    for(const auto& s:surfaces_) {
        if(s.boundary!=ThermalRadiationBoundary::Cavity) throw std::invalid_argument("Diffuse surface must own a cavity");
        const long double w=s.areaM2*s.emissivity;diagonal_[s.cavity]+=w;if(w>0) active_[s.cavity]=true;
    }
    auto& anchored=anchored_;
    for(std::size_t i=0;i<cavities;++i) if(active_[i]) anchored[root(i)]=true;
    for(std::size_t i=0;i<cavities;++i) {
        active_[i]=anchored[root(i)];
        if(!std::isfinite(diagonal_[i]) || !std::isfinite(boundary_[i])) throw std::invalid_argument("Radiation coefficient overflow");
    }
}
void DiffuseRadiation::setWorkBudget(std::size_t* work,std::size_t limit) {work_=work;limit_=limit;if(work) healthy_=true;}
bool DiffuseRadiation::tick() const {
    if(work_) {if(*work_>=limit_) {healthy_=false;return false;}++*work_;}return true;
}
void DiffuseRadiation::matrix(std::span<const long double> x,std::span<long double> out) const {
    for(std::size_t i=0;i<x.size();++i) out[i]=active_[i]?diagonal_[i]*x[i]:0;
    for(const auto& p:portals_) if(p.b && *p.b!=p.a && active_[p.a]) {
        out[p.a]-=p.areaM2*x[*p.b];out[*p.b]-=p.areaM2*x[p.a];
    }
}
bool DiffuseRadiation::solveField() const {
    if(!tick()) return false;
    std::fill(field_.begin(),field_.end(),0);
    long double scale=0;
    for(std::size_t i=0;i<rhs_.size();++i) {
        residual_[i]=active_[i]?rhs_[i]:0;scale+=std::abs(residual_[i]);
        z_[i]=active_[i]?residual_[i]/diagonal_[i]:0;direction_[i]=z_[i];
    }
    auto rz=dot(residual_,z_);
    for(std::size_t it=0;it<=256;++it) {
        long double error=0;for(auto r:residual_) error=std::max(error,std::abs(r));
        if(error<=1e-15L+1e-19L*scale) return true;
        if(it==256 || !tick()) {healthy_=false;return false;}
        matrix(direction_,product_);const auto den=dot(direction_,product_);
        if(!(den>0) || !std::isfinite(den)) {healthy_=false;return false;}
        const auto alpha=rz/den;
        for(std::size_t i=0;i<rhs_.size();++i) {
            field_[i]+=alpha*direction_[i];residual_[i]-=alpha*product_[i];
            z_[i]=active_[i]?residual_[i]/diagonal_[i]:0;
        }
        const auto next=dot(residual_,z_);
        for(std::size_t i=0;i<rhs_.size();++i) direction_[i]=z_[i]+next/rz*direction_[i];
        rz=next;
    }
    healthy_=false;return false;
}
bool DiffuseRadiation::freeze(std::span<const long double> t,std::span<long double> d,std::span<long double> source) {
    trialValid_=false;
    if(t.size()!=nodes_ || d.size()!=nodes_ || source.size()!=nodes_ || !healthy_) return false;
    long double maximum=0;
    for(const auto& s:surfaces_) {
        if(!std::isfinite(t[s.body]) || t[s.body]<0) return false;
        maximum=std::max(maximum,t[s.body]);
    }
    // A common positive slope keeps the eliminated operator symmetric/PSD.
    // The affine offsets recover exact sigma*T^4 at every nonlinear trial.
    slope_=4*sigma*maximum*maximum*maximum;
    rhs_=boundary_;
    for(std::size_t j=0;j<surfaces_.size();++j) {
        const auto& s=surfaces_[j];const auto v=t[s.body];
        const long double w=s.areaM2*s.emissivity;
        offset_[j]=slope_*v-sigma*v*v*v*v;
        rhs_[s.cavity]-=w*offset_[j];
    }
    if(!solveField()) return false;
    std::fill(d.begin(),d.end(),0);std::fill(source.begin(),source.end(),0);
    for(std::size_t j=0;j<surfaces_.size();++j) {
        const auto& s=surfaces_[j];const long double w=s.areaM2*s.emissivity;
        d[s.body]+=w*slope_;source[s.body]+=w*(offset_[j]+field_[s.cavity]);
    }
    rhs_=boundary_;
    for(const auto& s:surfaces_) {const auto v=t[s.body];rhs_[s.cavity]+=s.areaM2*s.emissivity*sigma*v*v*v*v;}
    if(!solveField()) return false;
    trial_=field_;
    trialValid_=std::isfinite(slope_);
    return trialValid_;
}
void DiffuseRadiation::apply(std::span<const long double> x,std::span<long double> out) const {
    std::fill(rhs_.begin(),rhs_.end(),0);std::fill(out.begin(),out.end(),0);
    for(const auto& s:surfaces_) rhs_[s.cavity]+=s.areaM2*s.emissivity*x[s.body];
    if(!solveField()) return;
    for(const auto& s:surfaces_) out[s.body]+=s.areaM2*s.emissivity*slope_*(x[s.body]-field_[s.cavity]);
}
long double DiffuseRadiation::trialPortalPowerW(std::size_t portal) const {
    const auto& p=portals_.at(portal);
    if(p.b && *p.b==p.a) return 0;
    const auto neighbour=p.b?trial_[*p.b]:sigma*std::pow(static_cast<long double>(p.spaceTemperatureK),4);
    return p.areaM2*(trial_[p.a]-neighbour);
}
std::optional<long double> DiffuseRadiation::boundaryPowerW() const {
    long double power=0;
    for(std::size_t i=0;i<portals_.size();++i) if(!portals_[i].b) power-=trialPortalPowerW(i);
    return power;
}
std::optional<long double> DiffuseRadiation::trialIrradiance(std::size_t cavity) const {
    if(!active_.at(cavity) || !trialValid_ || !healthy_) return std::nullopt;
    return trial_.at(cavity);
}
} // namespace station
