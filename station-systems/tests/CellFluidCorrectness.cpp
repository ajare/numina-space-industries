#include "CellFluidFixture.h"
#include <station/CellAerodynamics.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace station;
namespace {
using Q=std::array<double,7>;
void require(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
void near(double a,double b,double tolerance=2e-11) {
    require(std::abs(a-b)<=tolerance*std::max({1.,std::abs(a),std::abs(b)}),"Fluid oracle/symmetry mismatch");
}
// Independent textbook conservative HLLE: no local-pressure gauge, activity
// reference, precomputed weights or optimized reflecting-Wall formula.
Q flux(const CellFluidState& a,double av,const CellFluidState& b,double bv,int axis,int normal) {
    const auto pa=cellFluidPrimitive(a,av),pb=cellFluidPrimitive(b,bv);
    const double ua=normal*(axis==0?pa.velocityX:pa.velocityY),ub=normal*(axis==0?pb.velocityX:pb.velocityY);
    const double sl=std::min(ua-pa.soundSpeed,ub-pb.soundSpeed),sr=std::max(ua+pa.soundSpeed,ub+pb.soundSpeed);
    Q fa{},fb{},result{};
    for(int k=0;k<7;++k) {fa[k]=a.q[k]/av*ua;fb[k]=b.q[k]/bv*ub;}
    fa[4+axis]+=normal*pa.pressurePa;fb[4+axis]+=normal*pb.pressurePa;
    fa[6]+=pa.pressurePa*ua;fb[6]+=pb.pressurePa*ub;
    for(int k=0;k<7;++k) {
        if(sl>=0) result[k]=fa[k];
        else if(sr<=0) result[k]=fb[k];
        else result[k]=(sr*fa[k]-sl*fb[k]+sl*sr*(b.q[k]/bv-a.q[k]/av))/(sr-sl);
    }
    return result;
}
// Independent characteristic correction: form complete conservative HLL fan
// states and a shear eigenvector, rather than the optimized scalar star test.
Q shearResolvedFlux(const CellFluidState& a,double av,const CellFluidState& b,double bv,int axis,int normal) {
    auto result=flux(a,av,b,bv,axis,normal);
    const auto pa=cellFluidPrimitive(a,av),pb=cellFluidPrimitive(b,bv);
    if(!pa.resolvedGas || !pb.resolvedGas) return result;
    const double ua=normal*(axis==0?pa.velocityX:pa.velocityY),ub=normal*(axis==0?pb.velocityX:pb.velocityY);
    const double sl=std::min(ua-pa.soundSpeed,ub-pb.soundSpeed),sr=std::max(ua+pa.soundSpeed,ub+pb.soundSpeed);
    if(sl>=0 || sr<=0) return result;
    const int tangent=5-axis;
    const double rootA=std::sqrt(pa.density),rootB=std::sqrt(pb.density);
    const double t=(rootA*(axis==0?pa.velocityY:pa.velocityX)+rootB*(axis==0?pb.velocityY:pb.velocityX))/(rootA+rootB);
    const double u=(rootA*ua+rootB*ub)/(rootA+rootB);
    const double strength=b.q[tangent]/bv-a.q[tangent]/av-t*(pb.density-pa.density);
    const double restored=u>=0 ? (sr-u)/sr : (sl-u)/sl;
    Q correction{};correction[tangent]=-sl*sr/(sr-sl)*restored*strength;correction[6]=t*correction[tangent];
    Q fa{},fb{},star{},leftStar{},rightStar{};
    for(int k=0;k<7;++k) {fa[k]=a.q[k]/av*ua;fb[k]=b.q[k]/bv*ub;}
    fa[4+axis]+=normal*pa.pressurePa;fb[4+axis]+=normal*pb.pressurePa;
    fa[6]+=pa.pressurePa*ua;fb[6]+=pb.pressurePa*ub;
    for(int k=0;k<7;++k) {
        star[k]=(sr*b.q[k]/bv-sl*a.q[k]/av-fb[k]+fa[k])/(sr-sl);
        leftStar[k]=star[k]+correction[k]/sl;rightStar[k]=star[k]+correction[k]/sr;
    }
    auto validStar=[](const Q& q) {
        for(const auto x:q) if(!std::isfinite(x)) return false;
        double rho=0;for(int g=0;g<4;++g) {if(q[g]<0) return false;rho+=q[g];}
        return rho>0 && q[6]>=(q[4]*q[4]+q[5]*q[5])/(2*rho);
    };
    if(!validStar(leftStar) || !validStar(rightStar)) return result;
    for(int k=0;k<7;++k) result[k]+=correction[k];
    return result;
}
Q reflectedFlux(const CellFluidCell& c,int axis,int normal) {
    auto ghost=c.initial;ghost.q[4+axis]*=-1;
    return flux(c.initial,c.volumeM3,ghost,c.volumeM3,axis,normal);
}
void contactSpeedShearTransport() {
    // Exact inviscid shear wave: equal density/pressure/normal velocity,
    // different tangential velocity. Tangential transport must use the contact
    // speed, not acoustic-speed mixing into the neighbouring row/column.
    for(int axis:{0,1}) for(double speed:{0.,20.,-20.,600.,-600.}) {
        auto mesh=fluid_test::grid(axis==0?2:1,axis==0?1:2);
        for(std::size_t i=0;i<2;++i) {
            const auto mass=mesh.cells[i].initial.q[1];const double tangent=i? -35.:20.;
            mesh.cells[i].initial=makeCellFluidState({0,mass,0,0},293.15,axis==0?speed:tangent,axis==0?tangent:speed);
        }
        std::size_t face=SIZE_MAX;for(std::size_t i=0;i<mesh.faces.size();++i) if(mesh.faces[i].b!=SIZE_MAX) face=i;
        const auto& up=mesh.cells[speed<0?1:0];const auto p=cellFluidPrimitive(up.initial,up.volumeM3);
        Q expected{};for(int k=0;k<7;++k) expected[k]=up.initial.q[k]/up.volumeM3*speed;
        expected[4+axis]+=p.pressurePa;expected[6]+=p.pressurePa*speed;
        constexpr double dt=1e-8;CellFluidSolver fluid(mesh);
        require(fluid.advance(dt) && fluid.work().substeps==1,"Shear fixture did not take one step");
        for(int k=0;k<7;++k) {
            const double observed=fluid.faceTransfers()[face].q[k],want=mesh.faces[face].areaM2*dt*expected[k];
            require(std::abs(observed-want)<=2e-11*std::max({1.,std::abs(observed),std::abs(want)}),
                "Acoustic-speed numerical diffusion smears a contact-speed shear wave");
        }
    }
}
void singleStepOracle() {
    auto mesh=fluid_test::grid(3,2);
    for(std::size_t i=0;i<mesh.cells.size();++i)
        mesh.cells[i].initial=makeCellFluidState({.3+.02*i,.8+.03*i,.01,.001},260+11*i,15*double(i)-30,25-9*double(i));
    for(auto& f:mesh.faces) {
        if(f.b!=SIZE_MAX && f.axis==0) {f.openFraction=.3;f.dischargeCoefficient=.7;}
        if(f.b==SIZE_MAX && f.axis==0 && f.sign==1) {f.openFraction=.25;f.dischargeCoefficient=.8;}
    }
    std::vector<Q> rhs(mesh.cells.size()),transfers(mesh.faces.size());Q wall{};
    for(std::size_t i=0;i<mesh.faces.size();++i) {
        const auto& f=mesh.faces[i];const auto& a=mesh.cells[f.a];
        const auto open=f.areaM2*f.openFraction*f.dischargeCoefficient,blocked=f.areaM2-open;
        const auto ab=shearResolvedFlux(a.initial,a.volumeM3,f.b==SIZE_MAX?CellFluidState{}:mesh.cells[f.b].initial,
            f.b==SIZE_MAX?1:mesh.cells[f.b].volumeM3,f.axis,f.sign);
        const auto aw=reflectedFlux(a,f.axis,f.sign);
        Q bw{};if(f.b!=SIZE_MAX) bw=reflectedFlux(mesh.cells[f.b],f.axis,-f.sign);
        for(int k=0;k<7;++k) {
            transfers[i][k]=open*ab[k];rhs[f.a][k]-=open*ab[k]+blocked*aw[k];
            wall[k]-=blocked*(aw[k]+bw[k]);
            if(f.b!=SIZE_MAX) rhs[f.b][k]+=open*ab[k]-blocked*bw[k];
        }
    }
    constexpr double dt=1e-8;CellFluidSolver fluid(mesh);
    require(fluid.advance(dt) && fluid.work().substeps==1,"Oracle fixture did not take one Euler step");
    for(std::size_t i=0;i<mesh.cells.size();++i) for(int k=0;k<7;++k)
        near(fluid.states()[i].q[k],mesh.cells[i].initial.q[k]+dt*rhs[i][k],64*std::numeric_limits<double>::epsilon());
    for(std::size_t i=0;i<mesh.faces.size();++i) for(int k=0;k<7;++k)
        near(fluid.faceTransfers()[i].q[k],dt*transfers[i][k]);
    for(int axis=0;axis<2;++axis) near(fluid.wallImpulse()[axis],dt*wall[4+axis]);
}
struct Chamber {CellFluidMesh mesh;std::size_t inlet{},outlet{};};
Chamber chambers() {
    Chamber c{fluid_test::grid(4,3)};
    for(std::size_t i=0;i<c.mesh.faces.size();++i) {
        auto& f=c.mesh.faces[i];
        if(f.axis==0 && f.b!=SIZE_MAX && f.a%4==1) {
            f.openFraction=0;
            if(f.a/4==1) {c.inlet=i;f.dischargeCoefficient=.7;}
        }
        if(f.axis==0 && f.b==SIZE_MAX && f.a==7 && f.sign==1) {
            c.outlet=i;f.openFraction=.3;f.dischargeCoefficient=.7;
        }
    }
    return c;
}
CellFluidMesh transformed(CellFluidMesh mesh,int mode) {
    for(auto& c:mesh.cells) {
        if(mode==0) c.initial.q[4]*=-1;
        if(mode==1) std::swap(c.initial.q[4],c.initial.q[5]);
    }
    for(auto& f:mesh.faces) {
        if(mode==0 && f.axis==0) f.sign*=-1;
        if(mode==1) f.axis=1-f.axis;
        if(mode==2 && f.b!=SIZE_MAX) {std::swap(f.a,f.b);f.sign*=-1;}
    }
    return mesh;
}
std::array<long double,7> total(std::span<const CellFluidState> states) {
    std::array<long double,7> q{};for(const auto& s:states) for(int k=0;k<7;++k) q[k]+=s.q[k];return q;
}
void sourceAndRestoreCacheEquivalence() {
    auto mesh=fluid_test::grid(2,2);
    for(std::size_t i=0;i<mesh.cells.size();++i) {
        const auto nitrogen=mesh.cells[i].initial.q[1];
        mesh.cells[i].initial=makeCellFluidState({0,nitrogen,0,.1},273.15,10+7*i,20-3*double(i));
    }
    CellFluidSolver sourced(mesh);
    sourced.addSpecies(0,0,.3,310);sourced.removeSpecies(1,1,1);sourced.addHeat(2,1000);sourced.condenseVapour(3);
    for(std::size_t i=0;i<mesh.cells.size();++i) mesh.cells[i].initial=sourced.states()[i];
    CellFluidSolver reconstructed(mesh);
    require(sourced.advance(.001) && reconstructed.advance(.001),"Source/cache fixture refused");
    for(std::size_t i=0;i<mesh.cells.size();++i) for(int k=0;k<7;++k) near(sourced.states()[i].q[k],reconstructed.states()[i].q[k]);
    std::vector<CellFluidState> retained;for(const auto& c:mesh.cells) retained.push_back(c.initial);
    sourced.restore(retained);CellFluidSolver fresh(mesh);
    require(sourced.advance(.001) && fresh.advance(.001),"Restore/cache fixture refused");
    for(std::size_t i=0;i<mesh.cells.size();++i) for(int k=0;k<7;++k) near(sourced.states()[i].q[k],fresh.states()[i].q[k]);
}
void strongShearAndNearVacuum() {
    std::uint32_t seed=0x5a17b39u;
    auto random=[&] {seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return double(seed)/double(UINT32_MAX);};
    for(int trial=0;trial<8;++trial) {
        auto mesh=fluid_test::grid(8,8);
        for(std::size_t i=0;i<mesh.cells.size();++i) {
            const double density=std::pow(10.,-9+11*random()),temperature=i%17==0?0.:50+2950*random();
            mesh.cells[i].initial=makeCellFluidState({0,density*12,0,0},temperature,2000*random()-1000,2000*random()-1000);
            if(i%19==0) mesh.cells[i].initial={};
        }
        CellFluidSolver fluid(mesh);const auto initial=total(fluid.states());std::array<long double,2> walls{};
        for(int step=0;step<20;++step) {
            require(fluid.advance(.002),"Strong shear/near-vacuum advance refused");
            for(int axis=0;axis<2;++axis) walls[axis]+=fluid.wallImpulse()[axis];
            const auto current=total(fluid.states());
            for(int k:{0,1,2,3,6}) near(static_cast<double>(current[k]),static_cast<double>(initial[k]));
            for(int axis=0;axis<2;++axis)
                require(std::abs(current[4+axis]-initial[4+axis]-walls[axis])<=1e-7L+1e-12L*std::abs(walls[axis]),
                    "Strong shear momentum accounting failed");
            for(const auto& p:fluid.primitives()) require(std::isfinite(p.temperatureK) && p.temperatureK>=0
                && std::isfinite(p.pressurePa) && p.pressurePa>=0,"Shear correction lost positivity");
        }
        const auto retained=std::vector<CellFluidState>(fluid.states().begin(),fluid.states().end());
        require(!fluid.advance(1,1),"Strong shear work refusal missing");
        for(std::size_t i=0;i<retained.size();++i) require(retained[i].q==fluid.states()[i].q,"Shear refusal changed inventory");
    }
}
void twoDoorConservationAndSymmetry() {
    const auto c=chambers();CellFluidSolver base(c.mesh),mirror(transformed(c.mesh,0)),
        rotate(transformed(c.mesh,1)),reverseFaces(transformed(c.mesh,2)),alwaysActive(c.mesh);
    // Reapplying unchanged controls wakes every Cell without changing geometry
    // or inventory: compare the activity shortcut with full face evaluation.
    for(std::size_t i=0;i<c.mesh.faces.size();++i) alwaysActive.setOpening(i,c.mesh.faces[i].openFraction);
    const auto initial=total(base.states());std::array<long double,7> escaped{};std::array<long double,2> walls{};
    for(int step=0;step<30;++step) {
        if(step==10) for(auto* r:{&base,&mirror,&rotate,&reverseFaces,&alwaysActive}) r->setOpening(c.inlet,.3);
        for(auto* r:{&base,&mirror,&rotate,&reverseFaces,&alwaysActive}) require(r->advance(.1),"Two-Door symmetry fixture refused");
        for(std::size_t i=0;i<base.states().size();++i) for(int k=0;k<7;++k) {
            const auto q=base.states()[i].q[k];
            near(mirror.states()[i].q[k],k==4?-q:q);
            near(rotate.states()[i].q[k],base.states()[i].q[k==4?5:k==5?4:k]);
            near(reverseFaces.states()[i].q[k],q);near(alwaysActive.states()[i].q[k],q);
        }
        for(std::size_t i=0;i<c.mesh.faces.size();++i) if(c.mesh.faces[i].b==SIZE_MAX)
            for(int k=0;k<7;++k) escaped[k]+=base.faceTransfers()[i].q[k];
        for(int axis=0;axis<2;++axis) walls[axis]+=base.wallImpulse()[axis];
        const auto current=total(base.states());
        for(int k:{0,1,2,3,6}) near(static_cast<double>(current[k]+escaped[k]),static_cast<double>(initial[k]));
        for(int axis=0;axis<2;++axis)
            require(std::abs(current[4+axis]+escaped[4+axis]-walls[axis]-initial[4+axis])<1e-7L,
                "Two-Door momentum/Wall/exterior accounting failed");
        CellAerodynamicField force(base.states().size()),reflectedForce(base.states().size());
        force.update(base);reflectedForce.update(mirror);
        for(std::size_t i=0;i<base.states().size();++i) {
            const auto a=force.sample(base,i),b=reflectedForce.sample(mirror,i);
            near(a.forceN[0],-b.forceN[0]);near(a.forceN[1],b.forceN[1]);
        }
    }
}
}
int main() {try {contactSpeedShearTransport();singleStepOracle();twoDoorConservationAndSymmetry();sourceAndRestoreCacheEquivalence();strongShearAndNearVacuum();std::cout<<"Contact-speed shear, conservative flux oracle, two-Door conservation and symmetry passed\n";}
catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
