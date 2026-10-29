#include <nsi/core/Installation.h>
#include <serialization/SerializationException.h>
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <stdexcept>
#include <limits>

using I=nsi::core::Installation;
using S=I::SectorType;
using W=I::WallType;
namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class Fn> void refused(Fn fn) {
    bool threw=false; try { fn(); } catch (const std::exception&) { threw=true; }
    check(threw,"Expected refusal");
}
void roundTrip(const I& original) {
    I restored; restored.deserialize(original.serialize());
    check(restored==original,"Full Installation round trip");
    auto yaml=YAML::Load(original.serialize());
    check(!yaml["levels"][0]["sectors"][0]["area"],"Area is not serialized");
    check(!yaml["levels"][0]["sectors"][0]["level"],"Level reference is not serialized");
}
void topology() {
    I i(2,2); const auto l=i.level(0);
    check(l.cellCount()==4 && l.walls().size()==12,"Shared edge counts");
    check(l.sectors().size()==1 && l.sectors()[0].area==4,"Initial cached area");
    std::size_t exterior=0;
    for (auto w:l.walls()) if (w.type==W::Exterior) ++exterior;
    check(exterior==8,"Initial perimeter");
    i.setSector(0,0,"Room",1);
    i.toggleWall(0,l.verticalWall(1,0));
    check(i.level(0).sectors().size()==1,"A partial boundary does not split a room");
    i.toggleWall(0,i.level(0).verticalWall(1,1));
    check(i.level(0).sectors().size()==2,"Complete boundary splits room");
    check(i.level(0).sectors()[0].name=="Room" && i.level(0).sectors()[1].name.empty(),"Split tie goes to lowest coordinate");
    check(i.level(0).sectors()[0].area==2 && i.level(0).sectors()[1].area==2,"Split areas");
    i.setSector(0,1,"Other",1);
    i.toggleWall(0,i.level(0).verticalWall(1,0));
    check(i.level(0).sectors().size()==1 && i.level(0).sectors()[0].name=="Room","Merge tie metadata");
    i.setCell(0,1,0,S::Void);
    check(i.level(0).sectorAt(1,0).type==S::Void,"Cell paint changes Sector type");
    check(i.level(0).walls()[i.level(0).verticalWall(1,1)].type==W::Interior,"Unrelated Wall survives");
    roundTrip(i);

    I split(3,1); split.setSector(0,0,"Large",1);
    split.toggleWall(0,split.level(0).verticalWall(1,0));
    check(split.level(0).sectors()[1].name=="Large","Largest fragment retains name");
    check(split.level(0).sectors()[0].name.empty(),"Small fragment unnamed");
    split.toggleWall(0,split.level(0).verticalWall(1,0));
    check(split.level(0).sectors()[0].name=="Large","Largest merge contributor wins");
    split.setCell(0,0,0,S::Void);
    check(split.level(0).walls()[split.level(0).verticalWall(0,0)].type==W::None,"Void perimeter Wall removed");
    split.toggleWall(0,split.level(0).verticalWall(0,0));
    check(split.level(0).walls()[split.level(0).verticalWall(0,0)].type==W::None,"No Wall in Void");
    check(split.level(0).walls()[split.level(0).verticalWall(1,0)].type==W::Exterior,"Painting automatically outlines Interior/Void boundary");
    split.setCell(0,0,0,S::Interior);
    check(split.level(0).walls()[split.level(0).verticalWall(1,0)].type==W::None,"Extending floor removes obsolete Exterior Walls");
    check(split.level(0).walls()[split.level(0).verticalWall(0,0)].type==W::Exterior,"Painting restores grid perimeter Walls");
    roundTrip(split);

    I tall(3,1); tall.addLevel(3,1); tall.setSector(0,0,"Atrium",2);
    tall.toggleWall(0,tall.level(0).verticalWall(1,0));
    check(tall.level(0).sectors()[0].height==2 && tall.level(0).sectors()[1].height==2,
          "Split fragments inherit height");
    roundTrip(tall);

    // Serialized indices need not be in coordinate order: metadata tie
    // breaking must use geometry, not whichever Sector happens to be index 0.
    I tied(2,1); tied.toggleWall(0,tied.level(0).verticalWall(1,0));
    tied.setSector(0,0,"Left",1); tied.setSector(0,1,"Right",1);
    auto n=YAML::Load(tied.serialize());
    auto left=YAML::Clone(n["levels"][0]["sectors"][0]);
    n["levels"][0]["sectors"][0]=YAML::Clone(n["levels"][0]["sectors"][1]);
    n["levels"][0]["sectors"][1]=left;
    n["levels"][0]["cells"][0]["sector"]=1;
    n["levels"][0]["cells"][1]["sector"]=0;
    YAML::Emitter swapped; swapped<<n; tied.deserialize(swapped.c_str());
    tied.toggleWall(0,tied.level(0).verticalWall(1,0));
    check(tied.level(0).sectors()[0].name=="Left","Merge ties use lowest coordinate, not Sector index");
}
void volumes() {
    I i(2,2); const auto before=i;
    refused([&]{i.setSector(0,0,"Tall",2);}); check(i==before,"Height above top rejected transactionally");
    i.addLevel(3,2,-1,0);
    i.setSector(0,0,"Tall",2);
    auto tall=i;
    refused([&]{i.setCell(1,1,0,S::Interior);}); check(i==tall,"Upper floor conflict rejected");
    i.setCell(1,0,0,S::Interior); // world x=-1: outside lower footprint
    auto offset=i;
    refused([&]{i.setOffset(1,0,0);}); check(i==offset,"Offset conflict rejected");
    check(!i.removeTopLevel(),"Top Level with lower claims is cleared");
    check(i.levelCount()==2 && i.level(1).allVoid(),"Clearing retains top Level");
    check(i.level(1).offsetX()==-1 && i.level(1).width()==3,"Clearing preserves bounds/offset");
    check(i.level(1).sectors().size()==1 && i.level(1).sectors()[0].name.empty(),"Clearing resets metadata");
    for (auto w:i.level(1).walls()) check(w.type==W::None,"Clearing removes Walls");
    roundTrip(i);
    i.setSector(0,0,"Short",1);
    check(i.removeTopLevel() && i.levelCount()==1,"Unclaimed top Level removed");
    refused([&]{i.removeTopLevel();});
    refused([&]{i.setSector(0,0,"Bad",0);});
    refused([&]{i.addLevel(0,1);});
    refused([&]{I invalid(std::numeric_limits<std::uint32_t>::max(),2);});

    I overlap(2,1); overlap.addLevel(2,1); overlap.addLevel(2,1);
    overlap.setSector(0,0,"Tall",3);
    auto saved=overlap;
    refused([&]{overlap.setCell(1,1,0,S::Interior);}); check(overlap==saved,"Intermediate claim blocks floors");
    refused([&]{overlap.setSector(1,0,"Void",2);});
    roundTrip(overlap);
}
void partitionPersistence() {
    check(I(1,1).partitionType()==I::PartitionType::InsulatedPartition,"Creation default partition");
    for (auto type : {I::PartitionType::BarePartition, I::PartitionType::ThermalBreak, I::PartitionType::InsulatedPartition}) {
        I i(1,1); i.setPartitionType(type); roundTrip(i);
        check(I::partitionSpec(type).transmittanceWPerM2K>0,"Positive Partition U-value");
    }
    I i(1,1); i.setPartitionType(std::nullopt); roundTrip(i);
    check(YAML::Load(i.serialize())["partition"].as<std::string>()=="none","Explicit no partition");
    auto node=YAML::Load(I(1,1).serialize()); node.remove("partition");
    for (int version : {3,4}) {
        node["version"]=version; YAML::Emitter out; out<<node;
        I loaded; loaded.deserialize(out.c_str());
        check(!loaded.partitionType(),"Absent partition never applies creation default");
        roundTrip(loaded);
    }
    node["version"]=4; node["partition"]="Unknown";
    YAML::Emitter bad; bad<<node;
    auto before=i; refused([&]{i.deserialize(bad.c_str());});
    check(i==before,"Invalid partition rejected transactionally");
    refused([&]{i.setPartitionType(static_cast<I::PartitionType>(99));});
    check(i==before,"Invalid Partition enum rejected");
}
void hullPersistence() {
    check(I(1,1).hullType()==I::HullType::MultiLayerBlanket,"Creation default hull");
    for (auto type : {I::HullType::BareSkin, I::HullType::StandardInsulated, I::HullType::MultiLayerBlanket}) {
        I i(1,1); i.setHullType(type); roundTrip(i);
        const auto spec=I::hullSpec(type);
        check(spec.transmittanceWPerM2K>0 && spec.emissivity>0 && spec.absorptivity>0,
              "Positive calibrated Hull properties");
        check(YAML::Load(i.serialize())["version"].as<int>()==4,"Writer emits v4");
    }
    I i(1,1); i.setHullType(std::nullopt); roundTrip(i);
    check(YAML::Load(i.serialize())["hull"].as<std::string>()=="none","Explicit no hull");
    auto node=YAML::Load(I(1,1).serialize());
    node.remove("hull");
    for (int version : {3,4}) {
        node["version"]=version; YAML::Emitter out; out<<node;
        I loaded; loaded.deserialize(out.c_str());
        check(!loaded.hullType(),"Absent key never applies default");
        check(loaded.level(0)==I(1,1).level(0),"Legacy geometry unchanged");
        roundTrip(loaded);
    }
    node["version"]=4; node["hull"]="Unknown";
    YAML::Emitter bad; bad<<node;
    auto before=i; refused([&]{ i.deserialize(bad.c_str()); });
    check(i==before,"Invalid hull rejected transactionally");
    refused([&]{ i.setHullType(static_cast<I::HullType>(99)); });
    check(i==before,"Invalid Hull enum rejected");
}
void persistence() {
    I original(3,2); original.setName("name: # [] \" UTF-8 Ω\nnext");
    original.setCell(0,1,0,S::Void); original.addLevel(2,3,-4,5);
    original.setCell(1,1,2,S::Interior); original.setSector(1,1,"Upper",1);
    roundTrip(original);
    const auto valid=original.serialize();
    for (const auto& invalid:std::vector<std::string>{"", "name: Legacy", "version: 1\ndecks: []", valid+"\n---\nname: extra"}) {
        I target=original;
        try { target.deserialize(invalid); throw std::logic_error("Accepted old or invalid format"); }
        catch (const serialization::SerializationException&) {}
        check(target==original,"Invalid load preserves Installation");
    }
    auto bad=[&](auto change) {
        auto n=YAML::Load(valid); change(n); YAML::Emitter out; out<<n;
        I target=original; bool threw=false;
        try {target.deserialize(out.c_str());} catch (const serialization::SerializationException&) {threw=true;}
        check(threw && target==original,"Invalid structure rejected transactionally");
    };
    bad([](auto n){n["levels"][0]["cells"][0]["sector"]=-1;});
    bad([](auto n){n["levels"][0]["cells"][0]["sector"]=999;});
    bad([](auto n){n["levels"][0]["cells"].remove(0);});
    bad([](auto n){n["levels"][0]["walls"].remove(0);});
    bad([](auto n){n["levels"][0]["sectors"][0]["height"]=100;});
    bad([](auto n){n["levels"][0]["sectors"][0]["type"]="Unknown";});
    bad([](auto n){n["levels"][0]["cells"][0]["floor"]["unexpected"]=1;});
    bad([](auto n){n["levels"][0]["walls"][0]="Interior";});
    bad([](auto n){n["levels"][0]["width"]=-1;});
    bad([](auto n){n["levels"][0]["width"]=0;});
    // A sector index cannot group disconnected regions across a Wall.
    I pair(2,1); auto n=YAML::Load(pair.serialize());
    n["levels"][0]["walls"][pair.level(0).verticalWall(1,0)]="Interior";
    YAML::Emitter out; out<<n;
    refused([&]{pair.deserialize(out.c_str());});
    // Conversely, separate same-type Sectors must actually be separated.
    I separated(2,1); separated.toggleWall(0,separated.level(0).verticalWall(1,0));
    n=YAML::Load(separated.serialize()); n["levels"][0]["walls"][separated.level(0).verticalWall(1,0)]="None";
    YAML::Emitter merged; merged<<n; refused([&]{separated.deserialize(merged.c_str());});
}
}
int main() {
    try { topology(); volumes(); persistence(); hullPersistence(); partitionPersistence(); }
    catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
