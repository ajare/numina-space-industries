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
Q reflectedFlux(const CellFluidCell& c,int axis,int normal) {
    auto ghost=c.initial;ghost.q[4+axis]*=-1;
    return flux(c.initial,c.volumeM3,ghost,c.volumeM3,axis,normal);
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
        const auto ab=flux(a.initial,a.volumeM3,f.b==SIZE_MAX?CellFluidState{}:mesh.cells[f.b].initial,
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
int main() {try {singleStepOracle();twoDoorConservationAndSymmetry();std::cout<<"Independent HLLE flux, two-Door conservation and symmetry passed\n";}
catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
