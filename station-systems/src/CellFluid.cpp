#include "station/CellFluid.h"
#include "StationGasFlow.h"
#include "station/AtmospherePhase.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace station {
namespace {
constexpr double tinyMass=1e-18;
// Precompute unit conversions: repeated species divisions dominate this small
// kernel. Thermodynamic constants remain shared with the compartment solver.
constexpr std::array<double,4> gasR{StationUniversalGasConstantJPerMolK/(1000*StationMolarMassOxygenKgPerMol),
    StationUniversalGasConstantJPerMolK/(1000*StationMolarMassNitrogenKgPerMol),
    StationUniversalGasConstantJPerMolK/(1000*StationMolarMassCarbonDioxideKgPerMol),
    StationUniversalGasConstantJPerMolK/(1000*StationMolarMassWaterKgPerMol)};
inline GasMixture fastMixture(const std::array<double,4>& mass) {
    GasMixture m;
    for (int g=0;g<4;++g) {m.massKg+=mass[g];m.cpKJPerK+=mass[g]*StationGasCpKJPerKgK[g];m.gasConstantKJPerK+=mass[g]*gasR[g];}
    m.capacityKJPerK=m.cpKJPerK-m.gasConstantKJPerK;return m;
}
std::array<double,4> masses(const CellFluidState& s) { return {s.q[0],s.q[1],s.q[2],s.q[3]}; }
double kinetic(const CellFluidState& s,double m) { return m>0 ? (s.q[4]*s.q[4]+s.q[5]*s.q[5])/(2*m) : 0; }
bool finitePrimitive(const CellFluidPrimitive& p) {
    return std::isfinite(p.massKg) && std::isfinite(p.density) && std::isfinite(p.capacityJPerK)
        && std::isfinite(p.temperatureK) && std::isfinite(p.pressurePa)
        && std::isfinite(p.velocityX) && std::isfinite(p.velocityY) && std::isfinite(p.soundSpeed);
}
bool valid(const CellFluidState& s) {
    double m=0;
    for (int g=0;g<4;++g) { if (!std::isfinite(s.q[g]) || s.q[g]<0) return false;m+=s.q[g]; }
    if (!std::isfinite(m)) return false;
    if (!std::isfinite(s.q[4]) || !std::isfinite(s.q[5]) || !std::isfinite(s.q[6]) || s.q[6]<0) return false;
    return m>0 ? s.q[6]>=kinetic(s,m) : s.q[4]==0 && s.q[5]==0 && s.q[6]==0;
}
}
CellFluidState makeCellFluidState(const std::array<double,4>& mass,double t,double vx,double vy) {
    CellFluidState s;
    const auto mix=fastMixture(mass);
    for (int g=0;g<4;++g) s.q[g]=mass[g];
    s.q[4]=mix.massKg*vx;s.q[5]=mix.massKg*vy;
    s.q[6]=1000*mix.capacityKJPerK*t+kinetic(s,mix.massKg);
    if (!std::isfinite(t) || t<0 || !valid(s)) throw std::invalid_argument("Invalid initial fluid state");
    return s;
}
namespace {
CellFluidPrimitive primitive(const CellFluidState& s,double inverseVolume) {
    CellFluidPrimitive p;const auto mix=fastMixture(masses(s));
    p.massKg=mix.massKg;p.density=mix.massKg*inverseVolume;p.capacityJPerK=1000*mix.capacityKJPerK;
    if (mix.massKg<=tinyMass && s.q[6]<=1e-10) return p;
    if (!(mix.massKg>0) || !(p.capacityJPerK>0)) return p;
    // Two reciprocals replace repeated state/mixture divisions. The mesh
    // already caches inverse volume; sound speed is gamma * R_specific * T.
    const double inverseMass=1/mix.massKg,inverseCapacity=1/mix.capacityKJPerK;
    p.resolvedGas=true;
    p.velocityX=s.q[4]*inverseMass;p.velocityY=s.q[5]*inverseMass;
    const double kineticJ=0.5*(s.q[4]*s.q[4]+s.q[5]*s.q[5])*inverseMass;
    p.temperatureK=std::max(0.0,s.q[6]-kineticJ)*(0.001*inverseCapacity);
    p.pressurePa=1000*mix.gasConstantKJPerK*p.temperatureK*inverseVolume;
    const double gamma=mix.cpKJPerK*inverseCapacity;
    p.soundSpeed=std::sqrt(std::max(0.0,gamma*(1000*mix.gasConstantKJPerK*inverseMass)*p.temperatureK));
    return p;
}
}
CellFluidPrimitive cellFluidPrimitive(const CellFluidState& s,double volume) {return primitive(s,1/volume);}
CellFluidSolver::CellFluidSolver(CellFluidMesh mesh):mesh_(std::move(mesh)) {
    const auto n=mesh_.cells.size();state_.reserve(n);
    for (const auto& c:mesh_.cells) {
        if (!(c.volumeM3>0) || !std::isfinite(c.volumeM3) || !valid(c.initial)) throw std::invalid_argument("Invalid fluid Cell");
        state_.push_back(c.initial);inverseVolume_.push_back(1/c.volumeM3);
    }
    backup_=candidate_=delta_=state_;primitive_.resize(n);active_.resize(n);initialContacts_.resize(n);
    rate_.resize(mesh_.faces.size());transferred_.resize(mesh_.faces.size());
    for (const auto& f:mesh_.faces)
        if (f.a>=n || (f.b!=SIZE_MAX && f.b>=n) || f.a==f.b || f.axis<0 || f.axis>1
            || (f.sign!=1 && f.sign!=-1) || !(f.areaM2>0) || !std::isfinite(f.areaM2)
            || !std::isfinite(f.openFraction) || f.openFraction<0 || f.openFraction>1
            || !std::isfinite(f.dischargeCoefficient) || f.dischargeCoefficient<=0 || f.dischargeCoefficient>1)
            throw std::invalid_argument("Invalid fluid face");
    std::vector<std::array<double,4>> sides(n);
    for (const auto& f:mesh_.faces) {
        if (f.b!=SIZE_MAX && mesh_.cells[f.a].level!=mesh_.cells[f.b].level)
            throw std::invalid_argument("Fluid faces cannot cross Levels");
        const auto side=2*f.axis+(f.sign>0 ? 1 : 0);
        if (sides[f.a][side]!=0 || (f.b!=SIZE_MAX && sides[f.b][side^1]!=0))
            throw std::invalid_argument("Duplicate fluid Cell face");
        sides[f.a][side]=f.areaM2;
        if (f.b!=SIZE_MAX) sides[f.b][side^1]=f.areaM2;
    }
    for (const auto& side:sides)
        if (!(side[0]>0) || !(side[2]>0) || side[0]!=side[1] || side[2]!=side[3])
            throw std::invalid_argument("Fluid Cells require four geometrically closed faces");
    for (std::size_t i=0;i<n;++i) for (int axis=0;axis<2;++axis)
        areaRate_[axis]=std::max(areaRate_[axis],sides[i][2*axis]/mesh_.cells[i].volumeM3);
    for (const auto& f:mesh_.faces) if (f.b!=SIZE_MAX && f.openFraction>0)
        for (int g=0;g<4;++g) if (std::abs(state_[f.a].q[g]/mesh_.cells[f.a].volumeM3-state_[f.b].q[g]/mesh_.cells[f.b].volumeM3)>1e-13)
            initialContacts_[f.a]=initialContacts_[f.b]=1;
    referenceOrder_.resize(n);std::iota(referenceOrder_.begin(),referenceOrder_.end(),0);
    refresh();
    if (!std::isfinite(areaRate_[0]) || !std::isfinite(areaRate_[1])) throw std::invalid_argument("Fluid face/volume ratio exceeds finite range");
    for (const auto& p:primitive_) if (!finitePrimitive(p)) throw std::invalid_argument("Fluid Cell properties exceed finite range");
    rebaseReference();refresh();
}
void CellFluidSolver::rebaseReference() {
    if (state_.empty()) return;
    // A constant pressure reference cancels on each complete Cell boundary.
    // Rebase to the current background once per advance: uniform source changes
    // must not turn every resting Cell into an active pressure/composition wave.
    // A median-pressure representative avoids choosing a local source/breach
    // at the first Cell as the whole Level's background. Scratch is preallocated.
    const auto middle=referenceOrder_.begin()+referenceOrder_.size()/2;
    std::nth_element(referenceOrder_.begin(),middle,referenceOrder_.end(),
        [&](std::size_t a,std::size_t b){
            const auto pa=primitive_[a].pressurePa,pb=primitive_[b].pressurePa;
            return pa==pb ? a<b : pa<pb;
        });
    const auto reference=*middle;referencePressure_=primitive(state_[reference],inverseVolume_[reference]).pressurePa;
    for (int g=0;g<4;++g) referenceDensity_[g]=state_[reference].q[g]*inverseVolume_[reference];
}
void CellFluidSolver::refresh() {
    for (std::size_t i=0;i<state_.size();++i) {
        primitive_[i]=primitive(state_[i],inverseVolume_[i]);
        const auto& p=primitive_[i];const auto reference=referencePressure_;
        bool disturbed=std::abs(p.pressurePa-reference)>1e-11*std::max(1.0,reference)
            || std::abs(p.velocityX)+std::abs(p.velocityY)>1e-9;
        for (int g=0;g<4;++g) {
            const double expected=referenceDensity_[g]*mesh_.cells[i].volumeM3;
            disturbed=disturbed || std::abs(state_[i].q[g]-expected)>1e-12*std::max(1e-12,expected);
        }
        active_[i]=disturbed || initialContacts_[i];
    }
}
void CellFluidSolver::restore(std::span<const CellFluidState> states) {
    if (states.size()!=state_.size()) throw std::invalid_argument("Fluid restore size mismatch");
    for (std::size_t i=0;i<states.size();++i)
        if (!valid(states[i]) || !finitePrimitive(cellFluidPrimitive(states[i],mesh_.cells[i].volumeM3)))
            throw std::invalid_argument("Invalid restored fluid state");
    std::copy(states.begin(),states.end(),state_.begin());refresh();rebaseReference();refresh();
    work_={};diagnostic_.clear();wallImpulse_={};
    std::fill(transferred_.begin(),transferred_.end(),CellFluidState{});
}
void CellFluidSolver::setOpening(std::size_t face,double f) {
    if (!std::isfinite(f) || f<0 || f>1) throw std::invalid_argument("Invalid fluid opening fraction");
    auto& opening=mesh_.faces.at(face);opening.openFraction=f;
    initialContacts_[opening.a]=1;if (opening.b!=SIZE_MAX) initialContacts_[opening.b]=1;
}
double CellFluidSolver::addSpecies(std::size_t cell,std::size_t gas,double kg,double t) {
    if (gas>=4 || !std::isfinite(kg) || kg<0 || !std::isfinite(t) || t<0) throw std::invalid_argument("Invalid fluid gas addition");
    auto& s=state_.at(cell);std::array<double,4> mass{};mass[gas]=kg;
    const double added=1000*fastMixture(mass).capacityKJPerK*t;
    auto next=s;next.q[gas]+=kg;next.q[6]+=added;
    if (!valid(next)) throw std::invalid_argument("Fluid gas addition exceeds finite range");
    const auto p=cellFluidPrimitive(next,mesh_.cells[cell].volumeM3);
    if (!finitePrimitive(p)) throw std::invalid_argument("Fluid gas properties exceed finite range");
    s=next;primitive_[cell]=p;active_[cell]=1;
    return added;
}
CellFluidSolver::Removal CellFluidSolver::removeSpecies(std::size_t cell,std::size_t gas,double requested) {
    if (gas>=4 || !std::isfinite(requested) || requested<0) throw std::invalid_argument("Invalid fluid gas removal");
    auto& s=state_.at(cell);
    const double kg=std::min(requested,s.q[gas]);if (!(kg>0)) return {};
    const auto mixture=fastMixture(masses(s));
    // Sources use the retained quantities even below the transport resolution.
    const double vx=s.q[4]/mixture.massKg,vy=s.q[5]/mixture.massKg;
    const double temperature=std::max(0.0,s.q[6]-kinetic(s,mixture.massKg))/(1000*mixture.capacityKJPerK);
    std::array<double,4> mass{};mass[gas]=kg;
    const double beforeEnergy=s.q[6];
    const double removed=1000*fastMixture(mass).capacityKJPerK*temperature+0.5*kg*(vx*vx+vy*vy);
    auto next=s;
    next.q[gas]-=kg;next.q[4]-=kg*vx;next.q[5]-=kg*vy;next.q[6]-=removed;
    if (fastMixture(masses(next)).massKg==0) next.q={};
    if (!valid(next)) throw std::runtime_error("Fluid gas removal failed positivity check");
    const auto p=cellFluidPrimitive(next,mesh_.cells[cell].volumeM3);
    if (!finitePrimitive(p)) throw std::runtime_error("Fluid gas removal exceeds finite range");
    s=next;primitive_[cell]=p;active_[cell]=1;
    return {kg,beforeEnergy-s.q[6]};
}
void CellFluidSolver::addHeat(std::size_t cell,double joules) {
    auto& s=state_.at(cell);if (!std::isfinite(joules)) throw std::invalid_argument("Non-finite fluid heat");
    if (joules==0) return;
    const auto& old=primitive_[cell];const double mass=old.massKg;
    const double e=s.q[6]+joules;
    if (!std::isfinite(e) || mass==0) throw std::invalid_argument("Cannot apply fluid heat to vacuum or exceed finite energy");
    const double kineticJ=kinetic(s,mass);
    if (e<kineticJ) throw std::runtime_error("Fluid heat removal exceeds internal energy");
    auto next=s;next.q[6]=e;auto p=old;
    if (old.resolvedGas) {
        // Heat changes neither inventory nor velocity/capacity. Reuse those
        // cached quantities, but derive pressure directly from species (no
        // accumulated pressure-ratio drift and no approximate sound speed).
        double r=0;for (int g=0;g<4;++g) r+=s.q[g]*gasR[g];r*=1000;
        const double inverseCapacity=1/old.capacityJPerK;
        p.temperatureK=(e-kineticJ)*inverseCapacity;
        p.pressurePa=r*p.temperatureK*inverseVolume_[cell];
        p.soundSpeed=std::sqrt(std::max(0.0,(1+r*inverseCapacity)*(r/mass)*p.temperatureK));
    } else p=primitive(next,inverseVolume_[cell]);
    if (!std::isfinite(p.temperatureK) || !std::isfinite(p.pressurePa) || !std::isfinite(p.soundSpeed))
        throw std::invalid_argument("Fluid heat exceeds finite property range");
    s=next;primitive_[cell]=p;active_[cell]=1;
}
CellFluidSolver::Condensation CellFluidSolver::condenseVapour(std::size_t cell) {
    const auto before=state_.at(cell);const auto p=primitive_[cell];
    if (!p.resolvedGas || !(p.capacityJPerK>0) || !(before.q[3]>0)) return {};
    const auto phase=solveVapourThermalCoupling(p.capacityJPerK/1000,
        p.temperatureK-StationCelsiusOffset,0,before.q[3],mesh_.cells[cell].volumeM3,0);
    if (!(phase.condensedKg>0)) return {};
    try {
        const double latentJ=phase.condensedKg*StationLatentHeatVaporizationKJPerKg*1000;
        addHeat(cell,latentJ);
        const auto liquid=removeSpecies(cell,3,phase.condensedKg);
        return {liquid.kg,latentJ,liquid.energyJ};
    } catch (...) {
        state_[cell]=before;primitive_[cell]=p;active_[cell]=1;throw;
    }
}
bool CellFluidSolver::advance(double seconds,std::size_t maxSubsteps) {
    diagnostic_.clear();work_={};wallImpulse_={};
    std::fill(transferred_.begin(),transferred_.end(),CellFluidState{});
    if (!std::isfinite(seconds) || seconds<0) { diagnostic_="Invalid fluid duration";return false; }
    if (seconds==0 || state_.empty()) return true;
    std::copy(state_.begin(),state_.end(),backup_.begin());rebaseReference();refresh();
    auto fail=[&](const char* why) {diagnostic_=why;std::copy(backup_.begin(),backup_.end(),state_.begin());
        refresh();std::fill(transferred_.begin(),transferred_.end(),CellFluidState{});wallImpulse_={};return false;};
    // Exactly resting, sealed regions need no acoustic substeps. Pressure,
    // composition and temperature contacts still wake their local faces.
    bool resting=true;
    for (const auto& p:primitive_) if (p.velocityX!=0 || p.velocityY!=0) resting=false;
    for (const auto& f:mesh_.faces) if (f.openFraction>0) {
        const auto& a=primitive_[f.a];
        if (f.b==SIZE_MAX) {if (a.pressurePa>0) resting=false;continue;}
        const auto& b=primitive_[f.b];
        if (std::abs(a.pressurePa-b.pressurePa)>1e-11*std::max({1.0,a.pressurePa,b.pressurePa})) resting=false;
        for (int g=0;g<4;++g)
            if (std::abs(state_[f.a].q[g]/mesh_.cells[f.a].volumeM3-state_[f.b].q[g]/mesh_.cells[f.b].volumeM3)>1e-13) resting=false;
    }
    if (resting) {
        for (std::size_t i=0;i<mesh_.faces.size();++i) {
            const auto& f=mesh_.faces[i];const double pa=primitive_[f.a].pressurePa;
            const double pb=f.b==SIZE_MAX ? 0 : primitive_[f.b].pressurePa;
            const double openArea=f.areaM2*f.openFraction*f.dischargeCoefficient;
            transferred_[i].q[4+f.axis]=seconds*f.sign*0.5*(pa+pb)*openArea;
            wallImpulse_[f.axis]+=seconds*f.sign*(pb-pa)*(f.areaM2-openArea);
        }
        work_.resting=true;return true;
    }
    double remaining=seconds;
    while (remaining>0) {
        if (work_.substeps>=maxSubsteps) return fail("Fluid acoustic substep work limit exceeded");
        std::array<double,2> signal{};
        for (const auto& p:primitive_) {
            signal[0]=std::max(signal[0],p.soundSpeed+std::abs(p.velocityX));
            signal[1]=std::max(signal[1],p.soundSpeed+std::abs(p.velocityY));
        }
        // Directional global bounds include neighbour and reflected-wall waves.
        // Do not charge both directions for the sum of both velocity components.
        const double frequency=areaRate_[0]*signal[0]+areaRate_[1]*signal[1];
        double dt=frequency>0 ? std::min(remaining,0.8/frequency) : remaining;
        if (!(dt>0) || !std::isfinite(dt) || remaining-dt==remaining) return fail("Fluid acoustic step below numerical resolution");
        std::fill(delta_.begin(),delta_.end(),CellFluidState{});
        std::array<double,2> wallRate{};
        for (std::size_t index=0;index<mesh_.faces.size();++index) {
            auto& rate=rate_[index];rate.q={};const auto& f=mesh_.faces[index];
            const bool exterior=f.b==SIZE_MAX;
            if (!active_[f.a] && (exterior || !active_[f.b]) && !(exterior && f.openFraction>0)) {
                const double blocked=f.areaM2*(1-f.openFraction*f.dischargeCoefficient);
                rate.q[4+f.axis]=f.sign*referencePressure_*(f.areaM2-blocked);
                wallRate[f.axis]+=f.sign*((exterior ? 0 : primitive_[f.b].pressurePa)-primitive_[f.a].pressurePa)*blocked;
                continue;
            }
            ++work_.faceEvaluations;
            const auto& a=primitive_[f.a];const CellFluidPrimitive vacuum{};
            const auto& b=exterior ? vacuum : primitive_[f.b];
            const double normal=f.sign,ref=referencePressure_;
            const double ua=normal*(f.axis==0 ? a.velocityX : a.velocityY);
            const double ub=normal*(f.axis==0 ? b.velocityX : b.velocityY);
            const double openArea=f.areaM2*f.openFraction*f.dischargeCoefficient;
            const double blocked=f.areaM2-openArea;
            const auto wallPressure=[](const CellFluidPrimitive& p,double u) {
                return std::max(0.0,p.pressurePa+p.density*u*(u+std::abs(u)+p.soundSpeed));
            };
            const int momentum=4+f.axis;
            const double wa=wallPressure(a,ua)*blocked,wb=exterior ? 0 : wallPressure(b,-ub)*blocked;
            delta_[f.a].q[momentum]-=normal*(wa-ref*blocked);
            if (!exterior) delta_[f.b].q[momentum]+=normal*(wb-ref*blocked);
            wallRate[f.axis]+=normal*(wb-wa);
            if (!(openArea>0)) continue;
            const double left=std::min(ua-a.soundSpeed,ub-b.soundSpeed),right=std::max(ua+a.soundSpeed,ub+b.soundSpeed);
            double wl=0,wr=0,wd=0;
            if (left>=0) wl=1;
            else if (right<=0) wr=1;
            else {wl=right/(right-left);wr=-left/(right-left);wd=left*right/(right-left);}
            for (int k=0;k<7;++k) {
                const double qa=state_[f.a].q[k]*inverseVolume_[f.a];
                const double qb=exterior ? 0 : state_[f.b].q[k]*inverseVolume_[f.b];
                double fa=qa*ua,fb=qb*ub;
                if (k==momentum) {fa+=normal*a.pressurePa;fb+=normal*b.pressurePa;}
                if (k==6) {fa+=a.pressurePa*ua;fb+=b.pressurePa*ub;}
                rate.q[k]=openArea*(wl*fa+wr*fb+wd*(qb-qa));
                const double shifted=rate.q[k]-(k==momentum ? normal*ref*openArea : 0);
                delta_[f.a].q[k]-=shifted;
                if (!exterior) delta_[f.b].q[k]+=shifted;
            }
        }
        bool accepted=false;
        for (int retry=0;retry<16;++retry) {
            accepted=true;
            for (std::size_t i=0;i<state_.size();++i) {
                for (int k=0;k<7;++k) candidate_[i].q[k]=state_[i].q[k]+dt*delta_[i].q[k];
                if (!valid(candidate_[i])) {accepted=false;break;}
            }
            if (accepted) break;
            dt*=0.5;++work_.retries;
        }
        if (!accepted || !(dt>0) || remaining-dt==remaining) return fail("Fluid positivity refinement failed");
        state_.swap(candidate_);
        for (std::size_t f=0;f<rate_.size();++f) for (int k=0;k<7;++k) transferred_[f].q[k]+=dt*rate_[f].q[k];
        for (int axis=0;axis<2;++axis) wallImpulse_[axis]+=dt*wallRate[axis];
        ++work_.substeps;remaining=dt>=remaining ? 0 : remaining-dt;refresh();
    }
    return true;
}
} // namespace station
