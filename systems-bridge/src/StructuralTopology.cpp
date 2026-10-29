#include <nsi/systems/StructuralTopology.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>

namespace nsi::systems {
namespace {
using I=core::Installation;
using Point=StructuralPoint;
using Owner=StructuralOwner;
using Owners=std::array<std::optional<Owner>,2>;
std::size_t add(std::size_t a,std::size_t b) {
    if (b>SIZE_MAX-a) throw std::length_error("Structural topology allocation size overflow");
    return a+b;
}
std::size_t mul(std::size_t a,std::size_t b) {
    if (a && b>SIZE_MAX/a) throw std::length_error("Structural topology allocation size overflow");
    return a*b;
}
void limit(std::size_t n,std::size_t maximum,const char* what) {
    if (n>maximum) throw std::length_error(std::string("Structural topology ")+what+" limit exceeded");
}
Point shift(Point p,int axis,int d) {
    if (axis==0) p.x+=d; else if (axis==1) p.y+=d; else p.z+=d;
    return p;
}
const I::Sector* sector(const I& i,const std::optional<Owner>& o) {
    if (!o) return nullptr;
    const auto& l=i.level(o->level);
    return &l.sectorAt(o->worldX-l.offsetX(),o->worldY-l.offsetY());
}
I::WallType wall(const I& i,const Owner& o,int axis,const Point& p) {
    const auto& l=i.level(o.level);
    const auto x=o.worldX-l.offsetX(),y=o.worldY-l.offsetY();
    const auto w=axis==0 ? l.verticalWall(p.x-l.offsetX(),y) : l.horizontalWall(x,p.y-l.offsetY());
    return l.walls()[w].type;
}
struct Edge {
    Point a,b;
    std::size_t assembly{};
};
bool sameEdge(const Edge& a,const Edge& b) {return a.a==b.a && a.b==b.b;}
double length(const Edge& e) {
    return double(e.b.x-e.a.x)*core::CellWidthM+double(e.b.y-e.a.y)*core::CellDepthM
        +double(e.b.z-e.a.z)*core::LevelHeightM;
}
}
StructuralTopology expandStructuralTopology(const I& installation,const StructuralTopologyLimits& limits) {
    StructuralTopology out;
    if (!installation.hullType()) return out; // explicit complete geometry-thermal opt-out
    // Preflight before validate(), which itself constructs world-height claims.
    std::size_t claims=0;
    for (const auto& l:installation.levels()) for (const auto& c:l.cells()) {
        const auto& s=l.sectors()[c.sector];
        if (s.type==I::SectorType::Interior) {
            if (s.height<1) throw std::invalid_argument("Invalid structural height claim");
            claims=add(claims,std::size_t(s.height));
        }
    }
    limit(claims,limits.maxClaimedCells,"claimed-Cell");
    const auto claimBytes=mul(claims,192); // map node + validation claims, conservative payload allowance
    limit(claimBytes,limits.maxStorageBytes,"storage-byte");
    installation.validate();
    std::map<Point,Owner> occupied;
    for (std::size_t base=0;base<installation.levelCount();++base) {
        const auto& l=installation.level(base);
        for (std::size_t c=0;c<l.cellCount();++c) {
            const auto& s=l.sectors()[l.cells()[c].sector];
            if (s.type!=I::SectorType::Interior) continue;
            const auto x=std::int64_t(l.offsetX())+std::int64_t(c%l.width());
            const auto y=std::int64_t(l.offsetY())+std::int64_t(c/l.width());
            for (int h=0;h<s.height;++h) occupied.emplace(Point{x,y,std::int64_t(base)+h},Owner{base,x,y});
        }
    }
    const auto hull=core::nativeThermalSpec(*installation.hullType());
    const auto partition=installation.partitionType() ? std::optional(core::nativeThermalSpec(*installation.partitionType())) : std::nullopt;
    // Walk canonical faces once: shared faces are owned by the negative Cell.
    auto walk=[&](auto&& emit) {
        for (const auto& [cell,owner]:occupied) for (int axis=0;axis<3;++axis) for (int sign:{-1,1}) {
            const auto neighbour=occupied.find(shift(cell,axis,sign));
            if (neighbour!=occupied.end() && sign<0) continue;
            Owners owners;
            owners[sign<0 ? 1 : 0]=owner;
            if (neighbour!=occupied.end()) owners[1]=neighbour->second;
            const auto p=sign<0 ? cell : shift(cell,axis,1);
            const auto* a=sector(installation,owners[0]);const auto* b=sector(installation,owners[1]);
            if (axis==2) {
                // No ceiling inside a tall claim, but every actual upper floor is solid.
                if (a && b && owners[0]->level==owners[1]->level) continue;
            }
            const bool exterior=!a || !b;
            const double area=axis==2 ? core::CellWidthM*core::CellDepthM : core::CellWidthM*core::LevelHeightM;
            StructuralAssemblyId id{p,axis,StructuralPart::Panel};
            auto type=I::WallType::Interior;
            bool door=false;
            if (axis!=2) {
                type=I::WallType::None;
                for (const auto& o:owners) if (o) {
                    const auto t=wall(installation,*o,axis,p);
                    if (t!=I::WallType::None) type=t;
                    if (t==I::WallType::Door && p.z==std::int64_t(o->level)) door=true;
                }
            }
            if (type==I::WallType::None) {
                if (a!=b) emit(id,owners,area,nullptr,true,false);
                continue;
            }
            const auto* material=exterior ? &hull : (partition ? &*partition : nullptr);
            if (door) {
                // Fixed frame/above-aperture + retained leaf = one full face, not full face + leaf.
                if (material) {
                    id.part=StructuralPart::DoorFrame;emit(id,owners,area-core::DoorOpeningAreaM2,material,false,false);
                    id.part=StructuralPart::DoorLeaf;emit(id,owners,core::DoorOpeningAreaM2,material,false,false);
                }
                id.part=StructuralPart::Panel;emit(id,owners,core::DoorOpeningAreaM2,nullptr,true,true);
            } else if (material) emit(id,owners,area,material,false,false);
        }
    };
    std::size_t patches=0,openings=0;
    walk([&](auto,auto,double,const core::ThermalAssemblySpec*,bool opening,bool) {
        if (opening) openings=add(openings,1);else patches=add(patches,1);
    });
    limit(patches,limits.maxAssemblies,"assembly");
    const auto bodies=mul(patches,2),edges=mul(patches,4);
    // At most four physical panels meet an edge. Each side has at most three
    // neighbours per edge; + through and Door hinge. No dense surface matrix.
    const auto contactBound=mul(patches,26);
    std::size_t bytes=claimBytes;
    for (const auto [n,size]:{std::pair{patches,sizeof(StructuralAssembly)},
        {bodies,sizeof(station::ThermalBody)},{edges,sizeof(Edge)},
        {openings,sizeof(StructuralOpening)},{contactBound,sizeof(station::ThermalSolidContact)}}) bytes=add(bytes,mul(n,size));
    limit(bytes,limits.maxStorageBytes,"storage-byte");out.predictedStorageBytes=bytes;
    out.assemblies.reserve(patches);out.openings.reserve(openings);
    walk([&](auto id,auto owners,double area,const core::ThermalAssemblySpec* material,bool opening,bool door) {
        if (opening) out.openings.push_back({id,owners,area,door});
        else out.assemblies.push_back({id,area,*material,owners,{}});
    });
    std::sort(out.assemblies.begin(),out.assemblies.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    std::vector<Edge> seams;seams.reserve(edges);
    for (std::size_t n=0;n<patches;++n) {
        auto& a=out.assemblies[n];a.bodies={2*n,2*n+1};
        if (a.id.part==StructuralPart::DoorLeaf) continue; // leaf is hinged, not welded on four frame edges
        const auto p=a.id.origin;const int u=(a.id.axis+1)%3,v=(a.id.axis+2)%3;
        const auto pu=shift(p,u,1),pv=shift(p,v,1),puv=shift(pu,v,1);
        for (auto [x,y]:{std::pair{p,pu},{p,pv},{pu,puv},{pv,puv}}) {
            if (y<x) std::swap(x,y);
            seams.push_back({x,y,n});
        }
    }
    std::sort(seams.begin(),seams.end(),[](const Edge& a,const Edge& b){return std::tie(a.a,a.b,a.assembly)<std::tie(b.a,b.b,b.assembly);});
    auto contacts=[&](auto&& emit) {
        for (const auto& a:out.assemblies) emit(a.bodies[0],a.bodies[1],a.areaM2/a.material.throughResistanceM2KPerW);
        for (std::size_t start=0;start<seams.size();) {
            auto end=start+1;while (end<seams.size() && sameEdge(seams[start],seams[end])) ++end;
            for (auto i=start;i<end;++i) for (auto j=i+1;j<end;++j) {
                const auto& a=out.assemblies[seams[i].assembly];const auto& b=out.assemblies[seams[j].assembly];
                const double seam=length(seams[i]);
                double g=std::min(a.material.junctionConductanceWPerMK,b.material.junctionConductanceWPerMK)*seam/2;
                if (a.id.axis==b.id.axis) {
                    const double distance=(std::abs(a.id.origin.x-b.id.origin.x)*core::CellWidthM
                        +std::abs(a.id.origin.y-b.id.origin.y)*core::CellDepthM
                        +std::abs(a.id.origin.z-b.id.origin.z)*core::LevelHeightM);
                    // Sheet resistance from both half-panels plus finite seam resistance.
                    const double sheet=std::min(a.material.sheetConductanceWPerK,b.material.sheetConductanceWPerK)*seam/(2*distance);
                    g=g>0 && sheet>0 ? 1/(1/g+1/sheet) : 0;
                }
                for (unsigned sa=0;sa<2;++sa) for (unsigned sb=0;sb<2;++sb)
                    if ((a.id.axis==b.id.axis ? sa==sb
                        : sector(installation,a.owners[sa])==sector(installation,b.owners[sb])) && g>0)
                        emit(a.bodies[sa],b.bodies[sb],g);
            }
            start=end;
        }
        // Finite hinge seam; closing/opening never destroys leaf inventory.
        for (std::size_t n=0;n<patches;++n) if (out.assemblies[n].id.part==StructuralPart::DoorLeaf) {
            const auto& leaf=out.assemblies[n];
            auto key=leaf.id;key.part=StructuralPart::DoorFrame;
            const auto frame=std::lower_bound(out.assemblies.begin(),out.assemblies.end(),key,[](const auto& a,const auto& id){return a.id<id;});
            if (frame==out.assemblies.end() || frame->id!=key) throw std::logic_error("Structural Door leaf without frame");
            const double g=leaf.material.junctionConductanceWPerMK*core::DoorHeightM/2;
            if (g>0) for (unsigned s=0;s<2;++s) emit(leaf.bodies[s],frame->bodies[s],g);
        }
    };
    std::size_t count=0;contacts([&](auto,auto,double){count=add(count,1);});
    limit(count,limits.maxContacts,"solid-contact");
    if (count>contactBound) throw std::logic_error("Structural contact prediction exceeded");
    out.contacts.reserve(count);out.bodies.reserve(bodies);
    for (const auto& a:out.assemblies) {
        // Hull presets define first=inside, second=outside. Geometric side order is fixed.
        double first=a.material.firstSideCapacityFraction;
        if (!a.owners[0]) first=1-first;
        const double capacity=a.areaM2*a.material.arealCapacityJPerM2K;
        out.bodies.emplace_back(station::NativeThermalBodySettings{capacity*first});
        out.bodies.emplace_back(station::NativeThermalBodySettings{capacity*(1-first)});
    }
    contacts([&](auto a,auto b,double g){out.contacts.push_back({a,b,g});});
    station::validateThermalContacts(out.bodies.size(),0,0,out.contacts,{},{});
    return out;
}
} // namespace nsi::systems
