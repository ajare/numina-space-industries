#include <nsi/systems/Atmosphere.h>
#include <station/editor/EditorSession.h>

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using I = nsi::core::Installation;
using station::editor::EditorSession;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::string idAt(const I& installation, std::size_t level, std::uint32_t x, std::uint32_t y) {
    return nsi::systems::expandSector(installation, level,
        installation.level(level).cellData(x,y).sector).value().id;
}
std::set<std::string> ids(const I& installation) {
    std::set<std::string> result;
    for (const auto& atmosphere : nsi::systems::expandInstallation(installation)) {
        require(result.insert(atmosphere.id).second, "Atmosphere ids must be unique");
        require(atmosphere.id.size() <= station::MaxStationStoreIdLength, "Atmosphere id too long");
        require(atmosphere.id != station::StationExteriorAtmosphereId, "Reserved atmosphere id");
    }
    for (std::size_t level=0; level<installation.levelCount(); ++level) {
        std::set<std::int64_t> uids;
        for (const auto& sector : installation.level(level).sectors()) {
            require(sector.uid > 0 && uids.insert(sector.uid).second, "Sector uids must be unique and positive");
            if (sector.type == I::SectorType::Interior)
                require(result.contains("sector-" + std::to_string(level) + "-" + std::to_string(sector.uid)),
                    "Id must compose Level index and Sector uid");
        }
    }
    return result;
}
void splitMerge() {
    // Both traversal orders: the dominant fragment may be visited last.
    for (std::uint32_t boundary : {1u, 2u, 3u}) {
        I installation(4,1);
        const auto original = idAt(installation,0,0,0);
        const auto wall = installation.level(0).verticalWall(boundary,0);
        installation.toggleWall(0,wall);
        const auto splitIds = ids(installation);
        require(splitIds.size()==2, "Split must produce two distinct ids");
        const auto dominantX = boundary >= 2 ? 0u : 3u;
        require(idAt(installation,0,dominantX,0)==original, "Dominant fragment must retain identity");
        require(idAt(installation,0,dominantX==0 ? 3 : 0,0)!=original, "Other fragment needs fresh identity");
        installation.toggleWall(0,wall);
        require(ids(installation)==std::set<std::string>{original}, "Merge must recover pre-split identity");
    }
    // The lower uid belongs to the smaller contributor, not the metadata donor.
    I installation(5,1);
    installation.toggleWall(0,installation.level(0).verticalWall(3,0));
    const auto smaller = idAt(installation,0,2,0);
    installation.toggleCell(0,0,0);
    installation.toggleCell(0,1,0);
    const auto& level = installation.level(0);
    require(level.sectorAt(2,0).uid < level.sectorAt(4,0).uid
        && level.sectorAt(2,0).area < level.sectorAt(4,0).area,
        "Fixture requires lower uid on smaller contributor");
    installation.toggleWall(0,level.verticalWall(3,0));
    require(idAt(installation,0,4,0)==smaller, "Merge must choose lowest dominated uid, not largest contributor");
}
void painting() {
    EditorSession session;
    session.newDocument(5,1);
    session.toggleCell(0,0,0);
    const auto original = idAt(session.installation(),0,1,0);
    // The preceding Void Sector changes index as Cells are repainted.
    session.beginStroke(0,0,0);
    session.paintStroke(0,0);
    session.endStroke();
    require(idAt(session.installation(),0,1,0)==original, "Growing Sector must retain identity across index change");
    ids(session.installation());
    session.undo();
    require(idAt(session.installation(),0,1,0)==original, "Undo paint must retain identity");
    session.redo();
    require(idAt(session.installation(),0,1,0)==original, "Redo paint must retain identity");
    session.toggleCell(0,4,0);
    require(idAt(session.installation(),0,1,0)==original, "Shrinking Sector must retain identity");
    ids(session.installation());
}
void wallHistory() {
    EditorSession session;
    session.replaceDocument(I(std::vector<I::Level>{I::Level(4,2), I::Level(4,2)}));
    session.toggleWall(1,session.installation().level(1).verticalWall(2,0));
    session.toggleWall(1,session.installation().level(1).verticalWall(2,1));
    const auto before = ids(session.installation());
    require(before.size()==3, "Multi-Level fixture requires three compartments");
    const auto top = session.installation().level(0).verticalWall(1,0);
    const auto bottom = session.installation().level(0).verticalWall(1,1);
    session.beginWallStroke(0,top);
    session.paintWallStroke(top);
    require(ids(session.installation())==before, "Partial Wall drag must not change ids");
    session.paintWallStroke(bottom);
    session.endStroke();
    const auto split = ids(session.installation());
    require(split.size()==4 && std::includes(split.begin(),split.end(),before.begin(),before.end()),
        "Wall split must preserve all existing ids");
    for (int repeat=0; repeat<3; ++repeat) {
        session.undo(); require(ids(session.installation())==before, "Undo Wall drag ids");
        session.redo(); require(ids(session.installation())==split, "Redo Wall drag ids");
    }
    session.beginWallStroke(0,top);
    session.paintWallStroke(top);
    session.paintWallStroke(bottom);
    session.endStroke();
    require(ids(session.installation())==before, "Erase Wall drag restores pre-split identity");
    session.undo(); require(ids(session.installation())==split, "Undo erase restores every split id");
    session.redo(); require(ids(session.installation())==before, "Redo erase restores merged id");
}
void largeUid() {
    I installation(1,1);
    auto yaml = installation.serialize();
    const auto start = yaml.find("uid: ");
    require(start!=std::string::npos, "Serialized uid missing");
    const auto number = start + std::string("uid: ").size();
    yaml.replace(number,yaml.find('\n',number)-number,"9223372036854775807");
    installation.deserialize(yaml);
    require(ids(installation)==std::set<std::string>{"sector-0-9223372036854775807"},
        "Largest uid must fit atmosphere id limit");
}
}
int main() {
    try { splitMerge(); painting(); wallHistory(); largeUid(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
