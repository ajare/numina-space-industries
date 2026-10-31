#include <station/editor/EditorSession.h>
#include <station/editor/GridPicking.h>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace station::editor;
using I=nsi::core::Installation;
namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class Fn> void refused(Fn fn) {
    bool threw=false; try { fn(); } catch (const std::exception&) { threw=true; }
    check(threw,"Expected rejected edit");
}
void automaticWalls() {
    using W = I::WallType;
    EditorSession session;
    session.replaceDocument(I(std::vector<I::Level>{I::Level(3,2,I::SectorType::Void)}));
    const auto empty = session.installation();
    session.beginStroke(0,0,0);
    session.paintStroke(0,0);
    {
        const auto& level = session.installation().level(0);
        check(level.walls()[level.horizontalWall(0,0)].type == W::Exterior, "Painting creates top boundary Wall");
        check(level.walls()[level.horizontalWall(0,1)].type == W::Exterior, "Painting creates Interior/Void Wall");
        check(level.walls()[level.verticalWall(0,0)].type == W::Exterior, "Painting creates grid perimeter Wall");
        check(level.walls()[level.verticalWall(1,0)].type == W::Exterior, "Painting creates right boundary Wall");
    }
    session.paintStroke(1,0);
    session.paintStroke(1,0); // A revisit must not remove the new outline.
    session.endStroke();
    const auto floor = session.installation();
    const auto& level = floor.level(0);
    check(level.walls()[level.verticalWall(1,0)].type == W::None, "Extending floor removes obsolete Exterior Wall");
    check(level.walls()[level.verticalWall(2,0)].type == W::Exterior, "Extending floor moves boundary outward");
    check(level.sectorAt(0,0).area == 2 && level.cellData(0,0).sector == level.cellData(1,0).sector,
          "Obsolete outline does not partition the Sector");
    I loaded; loaded.deserialize(floor.serialize());
    check(loaded == floor, "Automatic Walls round trip");
    session.undo(); check(session.installation() == empty && !session.canUndo(), "One undo removes the whole stroke and its Walls");
    session.redo(); check(session.installation() == floor, "Redo restores automatic outline exactly");
    session.toggleWall(0,level.verticalWall(1,0));
    const auto partitioned = session.installation();
    session.toggleCell(0,2,0);
    check(session.installation().level(0).walls()[level.verticalWall(1,0)].type == W::Interior,
          "Painting preserves valid manually placed Interior Walls");
    session.undo(); check(session.installation() == partitioned, "Undo preserves authored partition");
    session.beginStroke(0,0,0);
    session.paintStroke(0,0);
    session.paintStroke(1,0);
    session.endStroke();
    check(session.installation().level(0).allVoid(), "Erasing painted Cells removes floor");
    for (auto wall : session.installation().level(0).walls())
        check(wall.type == W::None, "Erasing floor removes all now-invalid Walls");
    session.undo(); check(session.installation() == partitioned, "Undo erasure restores authored and automatic Walls");
}
void doorEditing() {
    EditorSession session; session.newDocument(5,5);
    const auto grid=session.installation().level(0);
    const auto door=grid.horizontalWall(2,2);
    for (unsigned x:{0,1,3,4}) session.toggleWall(0,grid.horizontalWall(x,2));
    const auto before=session.installation();
    session.setDoor(0,door,true);
    const auto placed=session.installation();
    check(placed.level(0).walls()[door].type==I::WallType::Door,"Session failed to place Door");
    session.undo(); check(session.installation()==before,"Undo Door failed");
    session.redo(); check(session.installation()==placed,"Redo Door failed");
    refused([&]{session.toggleWall(0,grid.horizontalWall(1,2));});
    check(session.installation()==placed,"Rejected support removal changed document");
    session.setEditingEnabled(false);
    session.setDoor(0,door,false);
    check(session.installation()==placed,"Door edit bypassed playback lock");
    session.setEditingEnabled(true);
    session.setDoor(0,door,false);
    check(session.installation()==before,"Door erasure failed");
    session.undo(); check(session.installation()==placed,"Undo Door erasure failed");
    session.redo(); check(session.installation()==before,"Redo Door erasure failed");
    session.setDoor(0,door,false); // No-op must not add history.
    session.undo(); check(session.installation()==placed,"No-op Door erasure added history");
}
void wallStrokes() {
    using W = I::WallType;
    EditorSession session;
    session.newDocument(2,2);
    session.setSector(0,0,"Hall",1);
    const auto before = session.installation();
    const auto top = before.level(0).verticalWall(1,0);
    const auto bottom = before.level(0).verticalWall(1,1);
    session.beginWallStroke(0,top);
    session.paintWallStroke(top);
    session.paintWallStroke(bottom);
    session.paintWallStroke(top); // Revisiting must not toggle it off.
    session.paintWallStroke(before.level(0).walls().size()); // Pointer outside grid.
    session.paintStroke(0,0); // Cell painting cannot leak into a Wall stroke.
    check(session.installation().level(0).sectors().size()==2,"Wall drag splits Sectors live");
    session.endStroke();
    const auto split = session.installation();
    check(split.level(0).walls()[top].type==W::Interior && split.level(0).walls()[bottom].type==W::Interior,
          "Wall drag adds every visited Interior Wall");
    session.undo(); check(session.installation()==before,"One undo reverses entire Wall drag and metadata");
    session.redo(); check(session.installation()==split,"One redo restores entire Wall drag");

    session.beginWallStroke(0,top);
    session.paintWallStroke(top);
    session.paintWallStroke(bottom);
    session.paintWallStroke(top);
    session.paintWallStroke(before.level(0).horizontalWall(0,1)); // Already absent: leave absent.
    check(session.installation().level(0).sectors().size()==1,"Erase drag merges Sectors live");
    session.endStroke();
    check(session.installation()==before,"Erase stroke remains erase when revisiting Walls");
    session.undo(); check(session.installation()==split,"Undo erase restores split Sector names and areas");
    session.redo(); check(session.installation()==before,"Redo erase restores merged Sector");

    const auto perimeter = before.level(0).horizontalWall(0,0);
    session.toggleWall(0,perimeter); // Start next stroke on an absent boundary edge.
    const auto missing = session.installation();
    session.beginWallStroke(0,perimeter);
    session.paintWallStroke(perimeter);
    session.paintWallStroke(top);
    check(session.installation().level(0).walls()[perimeter].type==W::Exterior,
          "Add stroke classifies perimeter as Exterior");
    check(session.installation().level(0).walls()[top].type==W::Interior,
          "Same stroke classifies internal edges as Interior");
    session.undo(); // Undo while held commits and reverses the whole gesture.
    check(!session.strokeActive() && session.installation()==missing,"Interrupted Wall stroke is one undo step");

    session.replaceDocument(I(std::vector<I::Level>{I::Level(2,2,I::SectorType::Void)}));
    session.beginWallStroke(0,0);
    session.paintWallStroke(0);
    session.paintWallStroke(1);
    session.endStroke();
    check(!session.canUndo() && !session.unsavedChanges(),"Ineligible Void Walls create no history");
    session.paintWallStroke(0); // Outside a stroke is a no-op.
    refused([&]{session.beginWallStroke(0,session.installation().level(0).walls().size());});
    refused([&]{session.beginWallStroke(1,0);});
    check(!session.strokeActive(),"Invalid Wall stroke start leaves no active gesture");
    session.beginWallStroke(0,0);
    session.endStroke();
    check(!session.canUndo(),"Empty Wall stroke creates no undo step");
}
void rejectedWallStroke() {
    EditorSession session;
    session.newDocument(3,1);
    session.addLevel(3,1);
    const auto boundary = session.installation().level(0).verticalWall(2,0);
    session.toggleWall(0,boundary);
    session.setSector(0,0,"Tall",2);
    session.toggleCell(1,2,0); // Upper floor above the shorter right-hand Sector.
    session.setName("Temporary");
    session.undo(); // Keep a redo branch to test transactional rejection.
    const auto before = session.installation();
    const auto perimeter = before.level(0).horizontalWall(0,0);
    session.beginWallStroke(0,perimeter);
    session.paintWallStroke(perimeter); // First edit succeeds.
    refused([&]{session.paintWallStroke(boundary);}); // Merged height would overlap upper floor.
    check(session.installation()==before && !session.strokeActive(),"Rejected Wall edit rolls back whole drag");
    check(session.canRedo(),"Rejected Wall drag preserves redo branch");
    session.redo(); check(session.installation().getName()=="Temporary","Redo content survives rejection");
    session.undo();
    session.toggleCell(1,2,0);
    check(session.installation().level(1).allVoid(),"Cell editing works after rejected Wall stroke");
}
void partitionHistory() {
    EditorSession session; session.newDocument(1,1);
    check(session.installation().partitionType()==I::PartitionType::InsulatedPartition,"New partition default");
    session.setPartitionType(I::PartitionType::BarePartition);
    check(session.unsavedChanges() && session.canUndo(),"Partition edit dirties document");
    session.setPartitionType(I::PartitionType::BarePartition);
    session.undo();
    check(session.installation().partitionType()==I::PartitionType::InsulatedPartition
        && !session.unsavedChanges() && !session.canUndo(),"Undo partition restores clean baseline; no redundant history");
    session.redo(); check(session.installation().partitionType()==I::PartitionType::BarePartition,"Redo partition");
    session.setPartitionType(I::PartitionType::ThermalBreak);
    session.undo(); check(session.installation().partitionType()==I::PartitionType::BarePartition,"Undo changed partition");
    session.redo(); check(session.installation().partitionType()==I::PartitionType::ThermalBreak,"Redo changed partition");
    session.setPartitionType(std::nullopt);
    check(!session.installation().partitionType(),"Explicit no partition command");
    session.undo(); check(session.installation().partitionType()==I::PartitionType::ThermalBreak,"Undo no partition");
    session.redo(); check(!session.installation().partitionType(),"Redo no partition");
}
void hullHistory() {
    EditorSession session; session.newDocument(1,1);
    check(session.installation().hullType()==I::HullType::MultiLayerBlanket,"New has blanket");
    session.setHullType(I::HullType::BareSkin);
    check(session.unsavedChanges() && session.canUndo(),"Hull edit dirties document");
    session.setHullType(I::HullType::BareSkin); // No extra history for same value.
    session.undo();
    check(session.installation().hullType()==I::HullType::MultiLayerBlanket
          && !session.unsavedChanges() && !session.canUndo(),"Undo hull restores baseline");
    session.redo(); check(session.installation().hullType()==I::HullType::BareSkin,"Redo hull");
    session.setHullType(std::nullopt);
    check(!session.installation().hullType(),"Explicit no hull command");
    session.undo(); check(session.installation().hullType()==I::HullType::BareSkin,"Undo no hull");
    session.redo(); check(!session.installation().hullType(),"Redo no hull");
}
void orbitHistory() {
    EditorSession session; session.newDocument(1,1);
    // A new document arrives already under a low Earth sky, so the author
    // sees day/night cycling without typing anything. The undo baseline is
    // that default, not an absent orbit.
    const I::Orbit kDefault{};
    check(session.installation().orbit()==kDefault,
          "New document must author the default low-orbit sky");
    // A synchronous ride: the height at which the derived Orbital period
    // comes back as the world's own day. Authored as a height, because the
    // period is no longer a thing to type.
    I::Orbit geo{};
    geo.heightAboveGroundM = geo.synchronousHeightM();
    session.setOrbit(geo);
    check(session.installation().orbit()==geo && session.unsavedChanges() && session.canUndo(),
          "Orbit edit did not dirty the document");
    session.setOrbit(geo); // No extra history for the same value.
    session.undo();
    check(session.installation().orbit()==kDefault && !session.unsavedChanges() && !session.canUndo(),
          "Undo orbit did not restore the default baseline");
    session.redo(); check(session.installation().orbit()==geo,"Redo orbit");
    session.setOrbit(std::nullopt);
    check(!session.installation().orbit(),"Explicit no orbit command");
    session.undo(); check(session.installation().orbit()==geo,"Undo no orbit");
    session.redo(); check(!session.installation().orbit(),"Redo no orbit");
    // Numbers the loader would refuse never reach the document at all.
    // Each case is built off the defaults through the one field it breaks,
    // so a change to the field order shows up as a changed test rather
    // than as a silently different sky.
    const auto untouched=session.installation();
    auto broken=[](auto tweak){ I::Orbit o; tweak(o); return o; };
    for (const auto invalid : {broken([](I::Orbit& o){o.stellarIrradianceWPerM2=0.0;}),
                              broken([](I::Orbit& o){o.heightAboveGroundM=0.0;}),
                              broken([](I::Orbit& o){o.planetMassKg=0.0;}),
                              broken([](I::Orbit& o){o.siderealRotationS=0.0;}),
                              broken([](I::Orbit& o){o.albedo=-0.1;}),
                              broken([](I::Orbit& o){o.albedo=1.1;})}) {
        bool refused=false;
        try { session.setOrbit(invalid); } catch (const std::exception&) { refused=true; }
        check(refused && session.installation()==untouched,
              "Invalid orbit reached the document");
    }
    // Both ends of the Albedo interval are authorable, and the value sticks.
    session.setOrbit(broken([](I::Orbit& o){o.albedo=0.0;}));
    check(session.installation().orbit()->albedo==0.0,"Albedo 0 was not authored");
    session.setOrbit(broken([](I::Orbit& o){o.albedo=1.0;}));
    check(session.installation().orbit()->albedo==1.0,"Albedo 1 was not authored");
    // The axial tilt and the phase offset ride the same command and the same undo
    // discipline as the rest of the record.
    const auto unshifted=session.installation().orbit();
    const I::Orbit tilted = broken([](I::Orbit& o){
        o.axialTiltDeg=23.44; o.phaseOffsetPercent=1234.5; });
    session.setOrbit(tilted);
    check(session.installation().orbit()==tilted,"Axial tilt and phase offset were not authored");
    session.undo();
    check(session.installation().orbit()==unshifted,"Undo did not restore the unshifted orbit");
    session.redo();
    check(session.installation().orbit()==tilted,"Redo did not restore the axial tilt and phase offset");
    // A tilt the model cannot sweep, and a phase offset that is not a
    // number, never reach the document.
    const auto authored=session.installation();
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double inf=std::numeric_limits<double>::infinity();
    for (const auto invalid : {broken([](I::Orbit& o){o.axialTiltDeg=90.0;}),
                              broken([](I::Orbit& o){o.axialTiltDeg=-90.5;}),
                              broken([&](I::Orbit& o){o.axialTiltDeg=nan;}),
                              broken([&](I::Orbit& o){o.phaseOffsetPercent=nan;}),
                              broken([&](I::Orbit& o){o.phaseOffsetPercent=inf;})}) {
        bool refused=false;
        try { session.setOrbit(invalid); } catch (const std::exception&) { refused=true; }
        check(refused && session.installation()==authored,
              "Invalid axial tilt or phase offset reached the document");
    }
    // A tilt just inside the pole is legal, however steep.
    session.setOrbit(broken([](I::Orbit& o){o.axialTiltDeg=89.9;}));
    check(session.installation().orbit()->axialTiltDeg==89.9,"A steep legal tilt was refused");
}
void history() {
    EditorSession session; session.newDocument(3,1);
    check(session.installation().level(0).width()==3,"Custom New dimensions");
    session.setSector(0,0,"Room",1);
    auto named=session.installation();
    const auto boundary=named.level(0).verticalWall(1,0);
    session.toggleWall(0,boundary);
    auto split=session.installation();
    check(split.level(0).sectors().size()==2 && split.level(0).sectors()[1].name=="Room","Wall command rebuilds Sectors");
    session.undo(); check(session.installation()==named,"Undo Wall restores metadata and areas");
    session.redo(); check(session.installation()==split,"Redo Wall exact snapshot");
    session.addLevel(4,2,-1,0);
    session.setSector(0,1,"Tall",2);
    session.toggleCell(1,0,0); // world x=-1 is outside the lower footprint
    auto with_upper=session.installation();
    session.beginStroke(1,0,0); // paints Void, then visit only already Void: real edit
    session.paintStroke(0,0);
    session.endStroke();
    session.undo(); check(session.installation()==with_upper,"Undo Cell stroke exact");
    // Painting Interior first succeeds outside the claim, then fails inside
    // the Tall Sector. The entire gesture and redo branch must survive.
    session.toggleCell(1,0,0);
    auto before=session.installation();
    const bool redo=session.canRedo();
    session.beginStroke(1,0,0);
    session.paintStroke(0,0);
    refused([&]{session.paintStroke(2,0);}); // world x=1 is claimed
    check(session.installation()==before && !session.strokeActive(),"Rejected stroke rolls back every Cell");
    check(session.canRedo()==redo,"Rejected stroke preserves history");
    session.undo(); check(session.installation()==with_upper,"Rejected stroke adds no undo entry");
    refused([&]{session.setOffset(1,1,0);});
    check(session.installation()==with_upper,"Rejected offset leaves state intact");
    check(!session.removeTopLevel(),"Removal of claimed top Level clears it");
    auto cleared=session.installation();
    check(cleared.levelCount()==2 && cleared.level(1).allVoid(),"Cleared top retains Level");
    session.undo(); check(session.installation()==with_upper,"Undo clearing restores Cells and Sectors");
    session.redo(); check(session.installation()==cleared,"Redo clearing exact");
    session.setSector(0,1,"Short",1);
    check(session.removeTopLevel(),"Unclaimed top removal succeeds");
    check(session.installation().levelCount()==1,"Top removed");
    session.undo(); check(session.installation().levelCount()==2,"Undo restores removed Level");
    session.setName("Installation");
    check(!session.canRedo(),"New metadata edit discards redo branch");
    session.newDocument(2,3);
    check(!session.canUndo() && !session.unsavedChanges(),"New resets baseline/history");
    refused([&]{session.newDocument(0,3);});
    check(session.installation().level(0).width()==2,"Invalid New leaves active document intact");
}
void picking() {
    I::Level level(3,2);
    check(nearestWall(level,0.05,0.6)==level.verticalWall(0,0),"Snap to left edge");
    check(nearestWall(level,1.4,0.05)==level.horizontalWall(1,0),"Snap to top edge");
    check(nearestWall(level,2.99,1.6)==level.verticalWall(3,1),"Snap to right edge");
    check(nearestWall(level,1.7,1.99)==level.horizontalWall(1,2),"Snap to bottom edge");
    check(nearestWall(level,1.03,0.7)==level.verticalWall(1,0),"Snap to internal vertical edge");
    check(nearestWall(level,0.7,1.03)==level.horizontalWall(0,1),"Snap to internal horizontal edge");
    check(nearestWall(level,0.5,0.5)==level.horizontalWall(0,1),"Picking tie is deterministic");
    check(!nearestWall(level,-0.1,1) && !nearestWall(level,1,2.1),"Outside has no Wall hit");
    check(!nearestWall(level,std::numeric_limits<double>::quiet_NaN(),1),"NaN has no hit");
}
// Deterministic edit replay catches stale areas, invalid connectivity and
// serialization drift across repeated splits, merges and undo branches.
void replay() {
    EditorSession session; session.newDocument(5,4);
    std::mt19937 random(1234);
    for (int step=0;step<100;++step) {
        auto before=session.installation();
        if (random()%2) session.toggleCell(0,random()%5,random()%4);
        else session.toggleWall(0,random()%before.level(0).walls().size());
        auto after=session.installation();
        std::vector<std::size_t> counts(after.level(0).sectors().size());
        for (const auto& c:after.level(0).cells()) ++counts.at(c.sector);
        for (std::size_t s=0;s<counts.size();++s)
            check(counts[s]==after.level(0).sectors()[s].area,"Area cache agrees with membership");
        I loaded; loaded.deserialize(after.serialize());
        check(loaded==after,"Edited topology round trips");
        if (before!=after) {
            session.undo(); check(session.installation()==before,"Replay undo exact");
            session.redo(); check(session.installation()==after,"Replay redo exact");
        }
    }
}
}
int main() {
    try {automaticWalls(); doorEditing(); wallStrokes(); rejectedWallStroke(); history(); hullHistory(); partitionHistory(); orbitHistory(); picking(); replay();}
    catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
