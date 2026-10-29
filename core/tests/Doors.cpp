#include <nsi/core/Installation.h>
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <stdexcept>

using I=nsi::core::Installation;
using W=I::WallType;
namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void refused(I& i,F edit) {
    const auto before=i;
    bool threw=false;
    try { edit(); } catch (const std::exception&) { threw=true; }
    check(threw,"Invalid Door edit accepted");
    check(i==before,"Rejected Door edit changed document");
}
void doors() {
    for (bool horizontal:{false,true}) {
        I i(5,5);
        const auto grid=i.level(0);
        auto edge=[&](unsigned p) { return horizontal ? grid.horizontalWall(p,2) : grid.verticalWall(2,p); };
        const auto door=edge(2);
        check(!i.level(0).canPlaceDoor(door),"Unsupported Door eligible");
        refused(i,[&]{i.setDoor(0,door,true);});
        for (unsigned p:{0,1,3,4}) i.toggleWall(0,edge(p));
        const auto before=i;
        check(i.level(0).canPlaceDoor(door),"Supported gap rejected");
        i.setDoor(0,door,true);
        check(i.level(0).walls()[door].type==W::Door,"Door not placed");
        check(i.level(0).sectors().size()==2,"Door failed to separate Sectors");
        const auto placed=i;
        i.setDoor(0,door,true);
        check(i==placed,"Door placement not idempotent");
        I loaded; loaded.deserialize(i.serialize());
        check(loaded==i,"Door round trip lost topology or metadata");
        refused(i,[&]{i.setDoor(0,edge(1),true);});
        refused(i,[&]{i.toggleWall(0,edge(1));});
        i.setDoor(0,door,false);
        check(i==before,"Removing Door failed to restore gap");
        i.toggleWall(0,door);
        i.setDoor(0,door,true);
        check(i.level(0).walls()[door].type==W::Door,"Cannot replace solid Wall with Door");

        // Malformed saves are refused transactionally, including doors whose
        // supports disappeared and doors illegally touching another door.
        auto yaml=YAML::Load(i.serialize());
        yaml["levels"][0]["walls"][edge(1)]="None";
        refused(loaded,[&]{loaded.deserialize(YAML::Dump(yaml));});
        yaml=YAML::Load(i.serialize());
        yaml["levels"][0]["walls"][edge(1)]="Door";
        refused(loaded,[&]{loaded.deserialize(YAML::Dump(yaml));});
    }
    I exterior(5,5);
    const auto top=exterior.level(0).horizontalWall(2,0);
    const auto skin=exterior.exteriorSkin(0,0).totalM2();
    exterior.setDoor(0,top,true);
    check(exterior.exteriorSkin(0,0).totalM2()==skin,"Exterior Door removed Hull area");
    refused(exterior,[&]{exterior.setDoor(0,exterior.level(0).horizontalWall(0,0),true);});
    refused(exterior,[&]{exterior.setCell(0,1,0,I::SectorType::Void);});
    auto standalone=exterior.level(0);
    const auto levelBefore=standalone;
    bool rejected=false;
    try { standalone.setCell(1,0,I::SectorType::Void); }
    catch (const std::exception&) { rejected=true; }
    check(rejected && standalone==levelBefore,"Standalone Cell edit left unsupported Door");
    // Perpendicular doors sharing an endpoint are forbidden even with supports.
    I corner(5,5); const auto grid=corner.level(0);
    for (unsigned x:{1,3}) corner.toggleWall(0,grid.horizontalWall(x,2));
    for (unsigned y:{0,1,2}) corner.toggleWall(0,grid.verticalWall(3,y));
    corner.setDoor(0,grid.horizontalWall(2,2),true);
    refused(corner,[&]{corner.setDoor(0,grid.verticalWall(3,1),true);});
    // A gap of one solid Wall between collinear doors is sufficient.
    I spaced(7,2); const auto g=spaced.level(0);
    for (unsigned x:{0,2,4}) spaced.toggleWall(0,g.horizontalWall(x,1));
    spaced.setDoor(0,g.horizontalWall(1,1),true);
    spaced.setDoor(0,g.horizontalWall(3,1),true);
    spaced.validate();
    I empty(std::vector<I::Level>{I::Level(5,5,I::SectorType::Void)});
    refused(empty,[&]{empty.setDoor(0,empty.level(0).horizontalWall(2,2),true);});
    check(!empty.level(0).canPlaceDoor(empty.level(0).walls().size()),"Out-of-range Door eligible");
}
}
int main() {
    try { doors(); }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
