#pragma once
// P1 cost experiment. Intentionally test-local; not an approved thermal backend.
#include "StructuralCostFixtures.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <stdexcept>
#include <tuple>
#include <vector>
namespace structural_cost {
inline void require(bool ok,const char* why) {if (!ok) throw std::runtime_error(why);}
constexpr double sigma=5.670374419e-8;
struct Body {double capacity{},temperature{293.15};};
struct Link {std::size_t a{},b{};double g{};};
struct Surface {std::size_t node{},cavity{SIZE_MAX};double area{},epsilon{},w{};};
struct Cavity {double aperture{},weight{},sum{},temperature{293.15},portalW{};};
struct Work {std::size_t intervals{},nonlinear{},cg{},matvec{};double residualJ{},energyResidualJ{};};
struct Model {
    std::vector<Body> bodies;
    std::vector<Link> links;
    std::vector<Surface> surfaces;
    std::vector<Cavity> cavities;
    std::size_t panels{},gasNodes{},throughLinks{},filmLinks{},localLinks{};
    // All numerical scratch allocated at construction. No dense surface matrix.
    std::vector<double> old,t,next,r,p,z,ap,diag,rhs;
    Work work;
    using Point=std::array<int,3>;
    struct Owner {std::size_t gas,cavity,level;};
    Model(const I& scene,int mode,bool vacuum=false,bool gradient=false) {
        if (!scene.hullType()) return;
        std::map<Point,Owner> occupied;
        for (std::size_t l=0;l<scene.levelCount();++l) {
            const auto& grid=scene.level(l);
            std::vector<std::size_t> groups(grid.sectors().size(),SIZE_MAX);
            for (std::size_t s=0;s<groups.size();++s) if (grid.sectors()[s].type==I::SectorType::Interior) {
                groups[s]=cavities.size();cavities.push_back({});
            }
            for (unsigned y=0;y<grid.depth();++y) for (unsigned x=0;x<grid.width();++x) {
                const auto& s=grid.sectorAt(x,y);if (s.type!=I::SectorType::Interior) continue;
                const auto g=bodies.size();
                bodies.push_back({vacuum || s.atmosphere==I::AtmospherePreset::Void ? 0.0 : 12*1.2*718*s.height,
                    gradient && x<grid.width()/2 ? 313.15 : 293.15});
                const auto cavity=groups[grid.cellData(x,y).sector];
                for (int dz=0;dz<s.height;++dz)
                    occupied.emplace(Point{int(x)+grid.offsetX(),int(y)+grid.offsetY(),int(l)+dz},Owner{g,cavity,l});
            }
        }
        gasNodes=bodies.size();
        // Physical panel edges identify local same-side sheet/seam contacts.
        // A corner/junction is connected only when panels share an entire edge.
        using Edge=std::pair<Point,Point>;
        std::map<Edge,std::vector<std::size_t>> edges;
        for (const auto& [pos,a]:occupied) for (int axis=0;axis<3;++axis) for (int sign:{-1,1}) {
            auto q=pos;q[axis]+=sign;auto it=occupied.find(q);
            const bool exterior=it==occupied.end();
            if (!exterior && (a.gas==it->second.gas || (axis<2 && a.cavity==it->second.cavity) || sign<0)) continue;
            if (!exterior && !scene.partitionType()) continue;
            auto corner=pos;if (sign>0) ++corner[axis];
            const double area=axis==2 ? 4 : 6;
            // Missing exterior horizontal Wall is a breach, not a solid face.
            bool opening=false;
            if (exterior && axis<2) {
                const auto& grid=scene.level(a.level);
                const int x=pos[0]-grid.offsetX(),y=pos[1]-grid.offsetY();
                if (x>=0 && y>=0 && x<int(grid.width()) && y<int(grid.depth())) {
                    const auto wall=axis==0 ? grid.verticalWall(x+(sign>0),y) : grid.horizontalWall(x,y+(sign>0));
                    opening=grid.walls()[wall].type==I::WallType::None;
                }
            }
            if (opening) {cavities[a.cavity].aperture+=area;continue;}
            const std::size_t n=bodies.size();++panels;
            const double capacity=area*(exterior ? 400 : 2000)/2;
            bodies.push_back({capacity});bodies.push_back({capacity});
            links.push_back({n,n+1,area/(exterior ? 19.8930878347 : 3.25194391034)});++throughLinks;
            const auto addSide=[&](std::size_t node,std::size_t cavity,std::size_t gas,double epsilon) {
                const auto si=surfaces.size();surfaces.push_back({node,cavity,area,epsilon});
                if (gas!=SIZE_MAX && bodies[gas].capacity>0) {
                    // C1 reference-pressure film (not an effective assembly U).
                    links.push_back({gas,node,19.9950194961*area});++filmLinks;
                }
                int u=(axis+1)%3,v=(axis+2)%3;
                for (int side=0;side<4;++side) {
                    auto start=corner,end=corner;
                    const int along=side<2 ? u : v,across=side<2 ? v : u;
                    start[across]+=side%2;end=start;++end[along];
                    edges[{start,end}].push_back(si);
                }
            };
            addSide(n,a.cavity,a.gas,0.8);
            addSide(n+1,exterior ? SIZE_MAX : it->second.cavity,exterior ? SIZE_MAX : it->second.gas,exterior ? 0.03 : 0.8);
        }
        if (mode>=1) for (const auto& [edge,sides]:edges) {
            const double length=edge.first[2]!=edge.second[2] ? 3 : 2;
            for (std::size_t i=0;i<sides.size();++i) for (std::size_t j=i+1;j<sides.size();++j) {
                const auto& a=surfaces[sides[i]];const auto& b=surfaces[sides[j]];
                if (a.node==b.node || a.cavity!=b.cavity) continue;
                // Provisional C1 Hull sheet + seam in series. Deliberately finite.
                links.push_back({a.node,b.node,1/(2/(0.01*length)+1/(0.05*length))});++localLinks;
            }
        }
        if (mode<2) {
            for (auto& s:surfaces) if (s.cavity!=SIZE_MAX) s.epsilon=0;
            for (auto& c:cavities) c.aperture=0;
        }
        const auto n=bodies.size();old.resize(n);t.resize(n);next.resize(n);r.resize(n);p.resize(n);
        z.resize(n);ap.resize(n);diag.resize(n);rhs.resize(n);
    }
    std::size_t bytes() const {
        return bodies.capacity()*sizeof(Body)+links.capacity()*sizeof(Link)+surfaces.capacity()*sizeof(Surface)
            +cavities.capacity()*sizeof(Cavity)+(old.capacity()+t.capacity()+next.capacity()+r.capacity()+p.capacity()
            +z.capacity()+ap.capacity()+diag.capacity()+rhs.capacity())*sizeof(double);
    }
    double energy() const {double sum=0;for (const auto& b:bodies) sum+=b.capacity*b.temperature;return sum;}
    static double secant(double a,double b) {return sigma*(a+b)*(a*a+b*b);}
    void coefficients(double dt) {
        for (auto& c:cavities) {c.weight=0;c.sum=0;c.portalW=c.aperture*secant(c.temperature,3);}
        for (std::size_t i=0;i<bodies.size();++i) diag[i]=bodies[i].capacity>0 ? bodies[i].capacity : 1;
        for (const auto& l:links) {diag[l.a]+=dt*l.g;diag[l.b]+=dt*l.g;}
        for (auto& s:surfaces) {
            s.w=s.area*s.epsilon*secant(t[s.node],s.cavity==SIZE_MAX ? 3 : cavities[s.cavity].temperature);
            if (s.cavity!=SIZE_MAX) cavities[s.cavity].weight+=s.w;
        }
        for (auto& c:cavities) c.weight+=c.portalW;
        for (const auto& s:surfaces) diag[s.node]+=dt*s.w*(s.cavity==SIZE_MAX || cavities[s.cavity].weight==0 ? 1 : 1-s.w/cavities[s.cavity].weight);
    }
    void multiply(const std::vector<double>& x,std::vector<double>& out,double dt) {
        ++work.matvec;
        for (std::size_t i=0;i<bodies.size();++i) out[i]=(bodies[i].capacity>0 ? bodies[i].capacity : 1)*x[i];
        for (const auto& l:links) {const double q=dt*l.g*(x[l.a]-x[l.b]);out[l.a]+=q;out[l.b]-=q;}
        for (auto& c:cavities) c.sum=0;
        for (const auto& s:surfaces) if (s.cavity!=SIZE_MAX) cavities[s.cavity].sum+=s.w*x[s.node];
        for (const auto& s:surfaces) {
            const double mean=s.cavity==SIZE_MAX || cavities[s.cavity].weight==0 ? 0 : cavities[s.cavity].sum/cavities[s.cavity].weight;
            out[s.node]+=dt*s.w*(x[s.node]-mean);
        }
    }
    static double dot(const std::vector<double>& a,const std::vector<double>& b) {return std::inner_product(a.begin(),a.end(),b.begin(),0.0);}
    void interval(double dt) {
        ++work.intervals;for (std::size_t i=0;i<bodies.size();++i) old[i]=t[i]=bodies[i].temperature;
        bool converged=false;
        for (int outer=0;outer<16;++outer) {
            ++work.nonlinear;coefficients(dt);
            multiply(old,ap,dt);
            for (std::size_t i=0;i<bodies.size();++i) rhs[i]=(bodies[i].capacity>0 ? bodies[i].capacity : 1)*old[i]-ap[i];
            for (const auto& s:surfaces) {
                const double boundary=s.cavity==SIZE_MAX ? 3 : cavities[s.cavity].weight>0 ? 3*cavities[s.cavity].portalW/cavities[s.cavity].weight : 0;
                rhs[s.node]+=dt*s.w*boundary;
            }
            std::fill(next.begin(),next.end(),0);r=rhs;
            for (std::size_t i=0;i<r.size();++i) p[i]=z[i]=r[i]/diag[i];
            double rz=dot(r,z);bool solved=false;
            for (int iter=0;iter<256;++iter) {
                double error=0;for (double value:r) error=std::max(error,std::abs(value));
                if (error<1e-8) {solved=true;break;}
                ++work.cg;multiply(p,ap,dt);const double alpha=rz/dot(p,ap);
                for (std::size_t i=0;i<r.size();++i) {next[i]+=alpha*p[i];r[i]-=alpha*ap[i];z[i]=r[i]/diag[i];}
                const double nr=dot(r,z),beta=nr/rz;rz=nr;
                for (std::size_t i=0;i<r.size();++i) p[i]=z[i]+beta*p[i];
            }
            require(solved,"P1 CG budget refused");
            double change=0;
            for (std::size_t i=0;i<t.size();++i) {next[i]+=old[i];change=std::max(change,std::abs(next[i]-t[i]));t[i]=next[i];}
            for (auto& c:cavities) c.sum=0;
            for (const auto& s:surfaces) if (s.cavity!=SIZE_MAX) cavities[s.cavity].sum+=s.w*t[s.node];
            for (auto& c:cavities) if (c.weight>0) {
                const double tc=(c.sum+c.portalW*3)/c.weight;change=std::max(change,std::abs(tc-c.temperature));c.temperature=tc;
            }
            if (change<1e-9) {converged=true;break;}
        }
        require(converged,"P1 nonlinear budget refused");
        // Independently evaluate the original nonlinear BE equation, not PCG's recurrence.
        for (std::size_t i=0;i<t.size();++i) r[i]=bodies[i].capacity*(t[i]-old[i]);
        for (const auto& l:links) {const double q=dt*l.g*(t[l.a]-t[l.b]);r[l.a]+=q;r[l.b]-=q;}
        double escaped=0;
        for (const auto& s:surfaces) {
            const double tc=s.cavity==SIZE_MAX ? 3 : cavities[s.cavity].temperature;
            const double q=dt*s.area*s.epsilon*sigma*(std::pow(t[s.node],4)-std::pow(tc,4));r[s.node]+=q;
            if (s.cavity==SIZE_MAX) escaped+=q;
        }
        for (const auto& c:cavities) escaped+=dt*c.aperture*sigma*(std::pow(c.temperature,4)-81);
        double energyResidual=escaped;
        for (std::size_t i=0;i<t.size();++i) {
            energyResidual+=bodies[i].capacity*(t[i]-old[i]);work.residualJ=std::max(work.residualJ,std::abs(r[i]));bodies[i].temperature=t[i];
        }
        work.energyResidualJ=std::max(work.energyResidualJ,std::abs(energyResidual));
        require(work.residualJ<1e-5 && work.energyResidualJ<1e-4,"P1 residual check refused");
    }
    void advance(double seconds) {
        require(std::isfinite(seconds) && seconds>0 && seconds<=1,"P1 study duration must be (0,1]");work={};
        const int count=int(std::ceil(seconds/0.1));for (int i=0;i<count;++i) interval(seconds/count);
    }
};
}
