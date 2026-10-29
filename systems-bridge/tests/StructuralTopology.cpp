#include <nsi/systems/StructuralTopology.h>
#include <nsi/systems/Atmosphere.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
using I=nsi::core::Installation;
using namespace nsi::systems;
void require(bool x,const char* why) {if (!x) throw std::runtime_error(why);}
void near(double a,double b) {
    if (std::abs(a-b)>=1e-8*std::max({1.0,std::abs(a),std::abs(b)}))
        throw std::runtime_error("Structural area/capacity/energy mismatch: "+std::to_string(a)+" vs "+std::to_string(b));
}
double area(const StructuralTopology& t) {double sum=0;for (const auto& a:t.assemblies) sum+=a.areaM2;return sum;}
double capacity(const StructuralTopology& t) {double sum=0;for (const auto& b:t.bodies) sum+=b.capacityJPerK();return sum;}
void invariants(const StructuralTopology& t) {
    require(t.bodies.size()==2*t.assemblies.size(),"Not two sides per assembly");
    std::set<StructuralAssemblyId> ids;
    std::set<std::pair<std::size_t,std::size_t>> links;
    for (const auto& a:t.assemblies) {
        require(ids.insert(a.id).second,"Duplicate physical patch");
        near(t.bodies[a.bodies[0]].capacityJPerK()+t.bodies[a.bodies[1]].capacityJPerK(),a.areaM2*a.material.arealCapacityJPerM2K);
        for (auto b:a.bodies) {near(t.bodies[b].temperatureK(),293.15);near(t.bodies[b].energyJ(),293.15*t.bodies[b].capacityJPerK());}
    }
    for (const auto& c:t.contacts) {
        require(c.a<t.bodies.size() && c.b<t.bodies.size() && c.a!=c.b && std::isfinite(c.conductanceWPerK) && c.conductanceWPerK>0,"Nonpassive/invalid contact");
        require(links.insert(std::minmax(c.a,c.b)).second,"Duplicate solid contact");
    }
    for (const auto& a:t.assemblies) require(links.contains(std::minmax(a.bodies[0],a.bodies[1])),"Missing through-assembly path");
}
I withoutGas(const I& i) {
    auto yaml=i.serialize();std::size_t pos=0;
    while ((pos=yaml.find("Earth-like",pos))!=std::string::npos) yaml.replace(pos,10,"Void");
    I out;out.deserialize(yaml);return out;
}
void check() {
    I one(1,1);auto t=expandStructuralTopology(one);invariants(t);
    require(t.assemblies.size()==6,"Single Cell needs six faces");near(area(t),32);near(capacity(t),32*400);
    require(t.contacts.size()==30,"Expected through paths and twelve two-sided local junctions");
    for (const auto& a:t.assemblies) for (unsigned side=0;side<2;++side) {
        const auto optics=a.surfaceMaterial(side);
        near(optics.emissivity,a.owners[side] ? a.material.firstSide.emissivity : a.material.secondSide.emissivity);
    }
    I pair(2,1);pair.toggleWall(0,pair.level(0).verticalWall(1,0));
    t=expandStructuralTopology(pair);invariants(t);
    near(area(t),58);near(capacity(t),52*400+6*2000);
    std::size_t shared=0;
    for (const auto& a:t.assemblies) if (a.owners[0] && a.owners[1]) {++shared;near(a.areaM2,6);}
    require(shared==1,"Shared Wall mass counted twice");
    // Roof skins have actual same-plane contact even across distinct Sectors.
    auto roof=[&](std::int64_t x) {
        const auto p=std::find_if(t.assemblies.begin(),t.assemblies.end(),[&](const auto& a){return a.id.axis==2 && a.id.origin==StructuralPoint{x,0,1};});
        require(p!=t.assemblies.end(),"Missing roof patch");return p->bodies[0];
    };
    const auto ra=roof(0),rb=roof(1);
    require(std::any_of(t.contacts.begin(),t.contacts.end(),[&](const auto& c){return std::minmax(c.a,c.b)==std::minmax(ra,rb);}),"Sector boundary broke physical roof sheet contact");
    I partial(2,2);partial.toggleWall(0,partial.level(0).verticalWall(1,0));
    require(partial.level(0).sectors().size()==1,"Expected same-Sector partial Wall fixture");
    auto partialTopology=expandStructuralTopology(partial);invariants(partialTopology);
    require(std::any_of(partialTopology.assemblies.begin(),partialTopology.assemblies.end(),[](const auto& a){return a.owners[0] && a.owners[1];}),"Same-Sector physical Wall disappeared");
    pair.setPartitionType(std::nullopt);t=expandStructuralTopology(pair);invariants(t);
    near(area(t),52);require(t.openings.empty(),"No-Partition Wall became a portal");
    pair.setHullType(std::nullopt);t=expandStructuralTopology(pair);
    require(t.bodies.empty() && t.contacts.empty() && t.openings.empty(),"hull:none did not opt out");

    I stacked(std::vector<I::Level>{I::Level(1,1),I::Level(1,1)});
    t=expandStructuralTopology(stacked);invariants(t);near(area(t),60);near(capacity(t),56*400+4*2000);
    shared=0;for (const auto& a:t.assemblies) if (a.owners[0] && a.owners[1]) {++shared;require(a.id.axis==2,"Expected shared floor");near(a.areaM2,4);}
    require(shared==1,"Shared floor counted twice");
    bool levelContact=false;
    for (const auto& c:t.contacts) if (c.a/2!=c.b/2) {
        const auto& a=t.assemblies[c.a/2];const auto& b=t.assemblies[c.b/2];
        if (a.id.origin.z!=b.id.origin.z) levelContact=true;
    }
    require(levelContact,"No cross-Level seams");
    I tall(2,1);tall.addLevel(1,1);tall.addLevel(2,1,1,0);
    tall.setSector(0,0,"Tall",2);tall.setCell(2,0,0,I::SectorType::Interior);tall.setCell(2,1,0,I::SectorType::Interior);
    t=expandStructuralTopology(tall);invariants(t);near(area(t),136);near(capacity(t),132*400+4*2000);
    require(t.assemblies.size()==25,"Tall/offset patches not split at Level boundaries");
    for (const auto& a:t.assemblies) require(a.id.axis!=2 || a.id.origin.z!=1,"Invented floor inside tall volume");
    // A tall room touches two independently owned rooms on its side, and only
    // the right-hand column has a real intermediate floor.
    I stepped(2,1);stepped.toggleWall(0,stepped.level(0).verticalWall(1,0));
    stepped.addLevel(2,1);stepped.setSector(0,stepped.level(0).cellData(0,0).sector,"Tall left",2);
    stepped.setCell(1,1,0,I::SectorType::Interior);
    auto steps=expandStructuralTopology(stepped);invariants(steps);
    near(area(steps),104);near(capacity(steps),88*400+16*2000);
    std::size_t sidePatches=0;
    for (const auto& a:steps.assemblies) if (a.id.axis==0 && a.id.origin.x==1 && a.owners[0] && a.owners[1]) ++sidePatches;
    require(sidePatches==2,"Tall side did not respect changing world-height ownership");
    const auto empty=withoutGas(tall);require(expandInstallation(empty).empty(),"Fixture still has gas inventory");
    auto evacuated=expandStructuralTopology(empty);invariants(evacuated);
    near(capacity(evacuated),capacity(t));require(evacuated.assemblies.size()==t.assemblies.size() && evacuated.contacts.size()==t.contacts.size(),"Void preset removed structure/connectivity");
    for (std::size_t n=0;n<t.assemblies.size();++n) require(evacuated.assemblies[n].id==t.assemblies[n].id,"IDs depend on gas inventory");

    I doors(3,1);doors.addLevel(3,1);doors.setSector(0,0,"Tall Door",2);
    const auto door=doors.level(0).horizontalWall(1,0);doors.setDoor(0,door,true);
    t=expandStructuralTopology(doors);invariants(t);near(area(t),120);near(capacity(t),120*400);
    double leaf=0,frame=0;for (const auto& a:t.assemblies) {
        if (a.id.part==StructuralPart::DoorLeaf) leaf+=a.areaM2;
        if (a.id.part==StructuralPart::DoorFrame) frame+=a.areaM2;
    }
    near(leaf,1.8);near(frame,4.2);
    I internalDoor(3,2);
    for (std::uint32_t x=0;x<3;++x) internalDoor.toggleWall(0,internalDoor.level(0).horizontalWall(x,1));
    internalDoor.setDoor(0,internalDoor.level(0).horizontalWall(1,1),true);
    auto internal=expandStructuralTopology(internalDoor);invariants(internal);
    double doorCapacity=0;
    for (const auto& a:internal.assemblies) if (a.id.part!=StructuralPart::Panel) {
        require(a.owners[0] && a.owners[1],"Interior Door classified as exterior");
        doorCapacity+=internal.bodies[a.bodies[0]].capacityJPerK()+internal.bodies[a.bodies[1]].capacityJPerK();
    }
    near(doorCapacity,6*2000);
    require(t.openings.size()==1 && t.openings[0].door,"Door opening metadata missing");near(t.openings[0].areaM2,1.8);
    doors.toggleWall(0,doors.level(0).horizontalWall(0,1));
    auto breached=expandStructuralTopology(doors);invariants(breached);
    near(area(breached),area(t)-12);double aperture=0;
    for (const auto& o:breached.openings) if (!o.door) aperture+=o.areaM2;
    near(aperture,12);require(breached.openings.size()==3,"Tall breach not split / aperture double counted");

    // Two disconnected but nearby structures must never gain a proximity bus.
    I disconnected(std::vector<I::Level>{I::Level(1,1),I::Level(1,1)});disconnected.setOffset(1,2,0);
    t=expandStructuralTopology(disconnected);invariants(t);
    for (const auto& c:t.contacts) {
        const auto& a=t.assemblies[c.a/2];const auto& b=t.assemblies[c.b/2];
        const auto ownerA=a.owners[0] ? *a.owners[0] : *a.owners[1];
        const auto ownerB=b.owners[0] ? *b.owners[0] : *b.owners[1];
        require(ownerA.level==ownerB.level,"Proximity created a structural link");
    }
    // Entirely geometric Void has no floor or structural inventory.
    I voidOnly(std::vector<I::Level>{I::Level(2,2,I::SectorType::Void)});
    require(expandStructuralTopology(voidOnly).assemblies.empty(),"Geometric Void gained structure");
    I negative(1,1);negative.setOffset(0,-100,-200);
    auto negativeTopology=expandStructuralTopology(negative);invariants(negativeTopology);near(area(negativeTopology),32);
    require(negativeTopology.assemblies.front().id.origin.x==-100,"Lost negative world coordinates");
    // Merge unrelated preceding Sectors: surviving source Cell changes Sector
    // index, but geometry/side identifiers and deterministic Reset remain stable.
    I reshuffle(4,1);reshuffle.toggleWall(0,reshuffle.level(0).verticalWall(1,0));
    reshuffle.toggleWall(0,reshuffle.level(0).verticalWall(2,0));
    auto before=expandStructuralTopology(reshuffle);
    const auto oldIndex=reshuffle.level(0).cellData(3,0).sector;
    reshuffle.toggleWall(0,reshuffle.level(0).verticalWall(1,0));
    require(oldIndex!=reshuffle.level(0).cellData(3,0).sector,"Fixture did not reshuffle Sector indices");
    auto after=expandStructuralTopology(reshuffle);invariants(after);
    for (const auto& a:before.assemblies) if (a.id.origin.x>=3) {
        auto found=std::find_if(after.assemblies.begin(),after.assemblies.end(),[&](const auto& b){return b.id==a.id;});
        require(found!=after.assemblies.end(),"Surviving physical ID lost on index reshuffle");
        for (unsigned side=0;side<2;++side) require(found->surfaceId(side)==a.surfaceId(side),"Surface ID changed on reshuffle");
    }
    after.bodies.front().addHeat(100);auto reset=expandStructuralTopology(reshuffle);invariants(reset);
    require(reset.contacts.size()==after.contacts.size(),"Reset topology is nondeterministic");
    for (std::size_t n=0;n<reset.contacts.size();++n) {
        const auto& a=after.contacts[n];const auto& b=reset.contacts[n];require(a.a==b.a && a.b==b.b && a.conductanceWPerK==b.conductanceWPerK,"Reset contacts changed");
    }
    // Limits are explicitly structural, including gas-free rooms, and checked
    // before corresponding output/scratch allocations, never a gas limit alias.
    auto rejects=[&](StructuralTopologyLimits limits,const char* word) {
        bool refused=false;try {(void)expandStructuralTopology(empty,limits);} catch (const std::length_error& e) {
            refused=std::string(e.what()).find("Structural topology")!=std::string::npos && std::string(e.what()).find(word)!=std::string::npos;
        }
        require(refused,"Missing separate structural capacity diagnostic");
    };
    StructuralTopologyLimits limits;limits.maxClaimedCells=1;rejects(limits,"claimed-Cell");
    limits={};limits.maxAssemblies=1;rejects(limits,"assembly");
    limits={};limits.maxContacts=1;rejects(limits,"solid-contact");
    limits={};limits.maxStorageBytes=1;rejects(limits,"storage-byte");
    limits={};limits.maxStorageBytes=expandStructuralTopology(empty).predictedStorageBytes-1;rejects(limits,"storage-byte");
}
}
int main() {try {check();std::cout<<"Structural topology passed\n";}catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
