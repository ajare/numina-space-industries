#include <nsi/core/Installation.h>
#include <serialization/SerializationException.h>
#include <yaml-cpp/yaml.h>
#include <array>
#include <cmath>
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
void compartmentLimit() {
    const auto message=[](std::size_t count) {
        return "Installation has " + std::to_string(count)
            + " atmosphere compartments; the supported maximum is 128. "
              "Reduce the number of Interior Sectors with non-Void atmosphere presets "
              "by merging compartments or using the Void atmosphere preset.";
    };
    const auto rejected=[&](auto fn,std::size_t count) {
        try { fn(); }
        catch (const std::invalid_argument& e) {
            check(e.what()==message(count),"Compartment refusal names actual count, maximum and remedy");
            return;
        }
        throw std::runtime_error("Expected compartment limit refusal");
    };
    I boundary(std::vector<I::Level>(128,I::Level(2,1)));
    boundary.validate(); roundTrip(boundary);
    for (std::size_t count : {129,146})
        rejected([&]{ I oversized(std::vector<I::Level>(count,I::Level(1,1))); },count);

    // Splitting a compartment or painting a new one cannot bypass validation.
    auto before=boundary;
    rejected([&]{ boundary.toggleWall(0,boundary.level(0).verticalWall(1,0)); },129);
    check(boundary==before,"Over-limit Wall edit preserves Installation");
    boundary.addLevel(1,1);
    before=boundary;
    rejected([&]{ boundary.setCell(128,0,0,S::Interior); },129);
    check(boundary==before,"Over-limit Cell edit preserves Installation");

    auto node=YAML::Load(I(1,1).serialize());
    const auto interior=YAML::Clone(node["levels"][0]);
    const auto empty=YAML::Load(I(std::vector<I::Level>{I::Level(1,1,S::Void)}).serialize())["levels"][0];
    auto unpressurised=YAML::Clone(interior);
    unpressurised["sectors"][0]["atmosphere"]="Void";
    node["levels"]=YAML::Node(YAML::NodeType::Sequence);
    for (int n=0;n<3;++n) node["levels"].push_back(YAML::Clone(interior));
    for (int n=0;n<150;++n) {
        node["levels"].push_back(YAML::Clone(empty));
        node["levels"].push_back(YAML::Clone(unpressurised));
    }
    YAML::Emitter mixed; mixed<<node;
    I loaded; loaded.deserialize(mixed.c_str()); loaded.validate(); roundTrip(loaded);
    check(loaded.levelCount()==303,"Many Void and Void-preset Sectors do not consume compartment budget");

    // An oversized saved document fails in core, before any runtime can be built.
    // Count past the measured agent-budget ceiling, not just one above the limit.
    node["levels"]=YAML::Node(YAML::NodeType::Sequence);
    for (int n=0;n<146;++n) node["levels"].push_back(YAML::Clone(interior));
    for (int version : {3,4}) {
        node["version"]=version; YAML::Emitter oversized; oversized<<node;
        before=loaded;
        bool threw=false;
        try { loaded.deserialize(oversized.c_str()); }
        catch (const serialization::SerializationException& e) {
            threw=true;
            check(e.what()==message(146),"Oversized load reports compartments, not an internal simulation error");
        }
        check(threw && loaded==before,"Oversized load rejected transactionally before simulation construction");
    }
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
void orbitPersistence() {
    using O=I::Orbit;
    // A new Installation authors the ISS-like sky its fields already carry,
    // following the MultiLayerBlanket new-Installation default: full Sun at
    // Earth, a 92.8 minute inertial period at ISS altitude, an Earth-like
    // Albedo and a coplanar star at Local noon.
    const O iss{1361.0,5570.0,6791000.0,0.3,0.0,0.0};
    check(I(1,1).orbit()==iss,"New Installation must author the ISS-like default orbit");
    check(I(1,1).orbit()->betaAngleRad==0.0,"Default orbit must start coplanar");
    check(I(1,1).orbit()->phaseOffsetS==0.0,"Default orbit must open at Local noon");
    // Absent still means no orbit, never a substituted default: the author
    // who clears the orbit gets the old dark sky back unchanged.
    I dark(1,1); dark.setOrbit(std::nullopt);
    check(!dark.orbit(),"Explicitly cleared orbit must stay absent");
    check(!YAML::Load(dark.serialize())["orbit"].IsDefined(),
          "Cleared orbit must not be written back out");
    I i(1,1); i.setOrbit(iss);
    check(i.orbit()==iss,"Authored orbit retained");
    roundTrip(i);
    const auto node=YAML::Load(i.serialize());
    check(node["orbit"]["stellarIrradianceWPerM2"].as<double>()==1361.0,"Irradiance persisted");
    check(node["orbit"]["orbitalPeriodS"].as<double>()==5570.0,"Orbital period persisted");
    check(node["orbit"]["radiusFromPlanetCentreM"].as<double>()==6791000.0,"Orbital radius persisted");
    check(node["orbit"]["albedo"].as<double>()==0.3,"Albedo persisted");
    // A geostationary author is just another set of numbers, not a mode.
    I geo(1,1); geo.setOrbit(O{1361.0,86164.0,42164000.0}); roundTrip(geo);
    // Albedo is authored, so a non-default one survives the round trip.
    I icy(1,1); icy.setOrbit(O{1361.0,5570.0,6791000.0,0.81}); roundTrip(icy);
    check(YAML::Load(icy.serialize())["orbit"]["albedo"].as<double>()==0.81,"Non-default Albedo persisted");
    // Both endpoints of the closed interval are legal and survive.
    I black(1,1); black.setOrbit(O{1361.0,5570.0,6791000.0,0.0}); roundTrip(black);
    I mirror(1,1); mirror.setOrbit(O{1361.0,5570.0,6791000.0,1.0}); roundTrip(mirror);
    // Beta and the phase offset default to the coplanar star and Local noon
    // at t = 0, and a non-default pair survives the round trip intact.
    check(O{}.betaAngleRad==0.0 && O{}.phaseOffsetS==0.0,
          "Beta and phase offset must default to coplanar and unshifted");
    I tilted(1,1); tilted.setOrbit(O{1361.0,5570.0,6791000.0,0.3,0.61,1234.5}); roundTrip(tilted);
    const auto tiltedNode=YAML::Load(tilted.serialize());
    check(tiltedNode["orbit"]["betaAngleRad"].as<double>()==0.61,"Beta angle persisted");
    check(tiltedNode["orbit"]["phaseOffsetS"].as<double>()==1234.5,"Phase offset persisted");
    // A negative Beta and a negative offset are both legal: they shift the
    // tilt and the clock the other way, not into a different model.
    I mirrored(1,1); mirrored.setOrbit(O{1361.0,5570.0,6791000.0,0.3,-0.61,-1234.5});
    roundTrip(mirrored);
    check(YAML::Load(mirrored.serialize())["orbit"]["betaAngleRad"].as<double>()==-0.61,
          "Negative Beta angle persisted");
    check(YAML::Load(mirrored.serialize())["orbit"]["phaseOffsetS"].as<double>()==-1234.5,
          "Negative phase offset persisted");
    // Zero is a value, not an absence: an authored zero Beta - the
    // worst-case eclipse - is written out rather than skipped.
    check(YAML::Load(i.serialize())["orbit"].IsMap()
              && YAML::Load(i.serialize())["orbit"]["betaAngleRad"].IsScalar()
              && YAML::Load(i.serialize())["orbit"]["phaseOffsetS"].IsScalar(),
          "Default Beta and phase offset must still be written");
    // Absent key loads as no orbit rather than a substituted default.
    auto bare=YAML::Load(i.serialize()); bare.remove("orbit");
    YAML::Emitter bareOut; bareOut<<bare;
    I loaded; loaded.deserialize(bareOut.c_str());
    check(!loaded.orbit(),"Absent orbit key never applies a default");
    // setOrbit refuses what the loader must refuse.
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double inf=std::numeric_limits<double>::infinity();
    const std::array<O,23> invalid{
        O{std::nextafter(O::MaxStellarIrradianceWPerM2, inf),5570.0,6791000.0},
        O{1e305,5570.0,6791000.0},O{std::numeric_limits<double>::max(),5570.0,6791000.0},
        O{nan,5570.0,6791000.0},O{inf,5570.0,6791000.0},
        O{0.0,5570.0,6791000.0},O{-1361.0,5570.0,6791000.0},
        O{1361.0,0.0,6791000.0},O{1361.0,-5570.0,6791000.0},
        O{1361.0,5570.0,nsi::core::PlanetRadiusM},
        O{1361.0,5570.0,1000.0},
        // Albedo is a fraction of the light that arrives: the closed
        // interval [0, 1] is the whole legal range.
        O{1361.0,5570.0,6791000.0,-0.1},O{1361.0,5570.0,6791000.0,1.1},
        O{1361.0,5570.0,6791000.0,nan},O{1361.0,5570.0,6791000.0,inf},
        // Beta is an angle between a direction and a plane: the open
        // interval (-pi/2, pi/2). The pole itself leaves no sweep to make.
        O{1361.0,5570.0,6791000.0,0.3,1.5707963267948966},
        O{1361.0,5570.0,6791000.0,0.3,-1.5707963267948966},
        O{1361.0,5570.0,6791000.0,0.3,1.58},
        O{1361.0,5570.0,6791000.0,0.3,3.1416},
        O{1361.0,5570.0,6791000.0,0.3,nan},
        O{1361.0,5570.0,6791000.0,0.3,inf},
        // The phase offset only shifts the cycle, so any finite value is
        // legal - but not a non-finite one.
        O{1361.0,5570.0,6791000.0,0.3,0.0,nan},
        O{1361.0,5570.0,6791000.0,0.3,0.0,inf}};
    for (const auto& orbit:invalid) refused([&]{ i.setOrbit(orbit); });
    check(i.orbit()==iss,"Rejected setOrbit left the authored orbit intact");
    auto badOrbit=[&](auto change) {
        auto n=YAML::Load(i.serialize()); change(n); YAML::Emitter out; out<<n;
        I target=I(1,1); target.setOrbit(iss);
        bool threw=false;
        try { target.deserialize(out.c_str()); } catch (const serialization::SerializationException&) { threw=true; }
        check(threw && target.orbit()==iss,"Invalid orbit rejected transactionally");
    };
    for (double irradiance : {1e50, O::MaxStellarIrradianceWPerM2}) {
        O large = iss;
        large.stellarIrradianceWPerM2 = irradiance;
        I source;
        source.setOrbit(large);
        I restored;
        restored.deserialize(source.serialize());
        check(restored.orbit() == source.orbit(), "Large supported irradiance round trips");
    }
    badOrbit([&](auto n){n["orbit"]["stellarIrradianceWPerM2"]=std::nextafter(O::MaxStellarIrradianceWPerM2,inf);});
    badOrbit([](auto n){n["orbit"]["stellarIrradianceWPerM2"]=1e305;});
    badOrbit([](auto n){n["orbit"]["stellarIrradianceWPerM2"]=std::numeric_limits<double>::max();});
    badOrbit([](auto n){n["orbit"]["stellarIrradianceWPerM2"]=0.0;});
    badOrbit([](auto n){n["orbit"]["stellarIrradianceWPerM2"]=-1.0;});
    badOrbit([](auto n){n["orbit"]["orbitalPeriodS"]=0.0;});
    badOrbit([](auto n){n["orbit"]["orbitalPeriodS"]=-5570.0;});
    badOrbit([](auto n){n["orbit"]["radiusFromPlanetCentreM"]=nsi::core::PlanetRadiusM;});
    badOrbit([](auto n){n["orbit"]["radiusFromPlanetCentreM"]=1000.0;});
    badOrbit([](auto n){n["orbit"].remove("stellarIrradianceWPerM2");});
    badOrbit([](auto n){n["orbit"].remove("orbitalPeriodS");});
    badOrbit([](auto n){n["orbit"].remove("radiusFromPlanetCentreM");});
    badOrbit([](auto n){n["orbit"].remove("albedo");});
    badOrbit([](auto n){n["orbit"]["albedo"]=-0.001;});
    badOrbit([](auto n){n["orbit"]["albedo"]=1.001;});
    badOrbit([](auto n){n["orbit"]["albedo"]=std::numeric_limits<double>::quiet_NaN();});
    badOrbit([](auto n){n["orbit"]["albedo"]=std::numeric_limits<double>::infinity();});
    badOrbit([](auto n){n["orbit"]["albedo"]="icy";});
    badOrbit([](auto n){n["orbit"]="ISS";});
    badOrbit([](auto n){n["orbit"]["orbitalPeriodS"]="92 minutes";});
    badOrbit([](auto n){n["orbit"]["stellarIrradianceWPerM2"]=std::numeric_limits<double>::infinity();});
    badOrbit([](auto n){n["orbit"]["stellarIrradianceWPerM2"]=std::numeric_limits<double>::quiet_NaN();});
    // Beta and the phase offset are validated on the way in exactly as the
    // rest of the record is.
    badOrbit([](auto n){n["orbit"]["betaAngleRad"]=1.5707963267948966;});
    badOrbit([](auto n){n["orbit"]["betaAngleRad"]=-1.58;});
    badOrbit([](auto n){n["orbit"]["betaAngleRad"]=std::numeric_limits<double>::quiet_NaN();});
    badOrbit([](auto n){n["orbit"]["betaAngleRad"]=std::numeric_limits<double>::infinity();});
    badOrbit([](auto n){n["orbit"]["betaAngleRad"]="steep";});
    badOrbit([](auto n){n["orbit"]["phaseOffsetS"]=std::numeric_limits<double>::quiet_NaN();});
    badOrbit([](auto n){n["orbit"]["phaseOffsetS"]=std::numeric_limits<double>::infinity();});
    badOrbit([](auto n){n["orbit"]["phaseOffsetS"]="half an orbit";});
    // A document authored before Beta and the phase offset existed is not a
    // partial record: absent means the documented default, which is the
    // very behaviour that was current when it was written.
    {
        auto n=YAML::Load(i.serialize());
        n["orbit"].remove("betaAngleRad"); n["orbit"].remove("phaseOffsetS");
        YAML::Emitter out; out<<n;
        I target; target.deserialize(out.c_str());
        check(target.orbit().has_value() && target.orbit()->betaAngleRad==0.0
                  && target.orbit()->phaseOffsetS==0.0,
              "A pre-Beta orbit must load as coplanar and unshifted");
    }
}
}
int main() {
    try { topology(); volumes(); persistence(); hullPersistence(); partitionPersistence(); orbitPersistence(); compartmentLimit(); }
    catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
