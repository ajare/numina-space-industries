#include <nsi/core/Installation.h>
#include <serialization/SerializationException.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <climits>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace nsi::core {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::invalid_argument(message);
}
std::size_t gridSize(std::uint32_t w, std::uint32_t d) {
    require(w > 0 && d > 0, "Level dimensions must be positive");
    const auto cells = std::uint64_t(w) * d;
    const auto walls = std::uint64_t(w) * (std::uint64_t(d)+1) + (std::uint64_t(w)+1)*d;
    require(cells <= INT_MAX && walls <= INT_MAX &&
            cells <= std::numeric_limits<std::size_t>::max()/sizeof(Installation::Cell),
            "Level dimensions overflow supported grid size");
    return static_cast<std::size_t>(cells);
}
const char* typeName(Installation::SectorType t) {
    return t == Installation::SectorType::Interior ? "Interior" : "Void";
}
const char* presetName(Installation::AtmospherePreset p) {
    return p == Installation::AtmospherePreset::Void ? "Void" : "Earth-like";
}
const char* typeName(Installation::WallType t) {
    switch (t) {
        case Installation::WallType::Interior: return "Interior";
        case Installation::WallType::Exterior: return "Exterior";
        case Installation::WallType::Door: return "Door";
        default: return "None";
    }
}
void mapNode(const YAML::Node& n, const std::string& where) {
    require(n.IsMap(), where + " must be a map");
    std::set<std::string> keys;
    for (const auto& p : n) {
        require(p.first.IsScalar(), where + " has a non-scalar key");
        require(keys.insert(p.first.as<std::string>()).second, where + " has duplicate keys");
    }
}
}

Installation::Level::Level(std::uint32_t w, std::uint32_t d, SectorType initial)
    : width_(w), depth_(d), cells_(gridSize(w,d)),
      sectors_{{"", initial, 1, cells_.size(), 0, 1}},
      walls_(std::size_t(w)*(std::size_t(d)+1)+(std::size_t(w)+1)*d) {
    require(initial == SectorType::Interior || initial == SectorType::Void, "Invalid Cell type");
    // A Level seeded with Void cells owns no volume, so its Sector carries the
    // Void atmosphere rather than the pressurized default.
    if (initial == SectorType::Void) sectors_[0].atmosphere = AtmospherePreset::Void;
    if (initial == SectorType::Interior) {
        for (std::uint32_t x=0; x<w; ++x) {
            walls_[horizontalWall(x,0)].type = WallType::Exterior;
            walls_[horizontalWall(x,d)].type = WallType::Exterior;
        }
        for (std::uint32_t y=0; y<d; ++y) {
            walls_[verticalWall(0,y)].type = WallType::Exterior;
            walls_[verticalWall(w,y)].type = WallType::Exterior;
        }
    }
}
bool Installation::Level::contains(std::uint32_t x, std::uint32_t y) const noexcept {
    return x<width_ && y<depth_;
}
std::size_t Installation::Level::index(std::uint32_t x, std::uint32_t y) const {
    if (!contains(x,y)) throw std::out_of_range("Cell outside Level");
    return std::size_t(y)*width_+x;
}
const Installation::Cell& Installation::Level::cellData(std::uint32_t x, std::uint32_t y) const {
    return cells_[index(x,y)];
}
const Installation::Sector& Installation::Level::sectorAt(std::uint32_t x, std::uint32_t y) const {
    return sectors_.at(cellData(x,y).sector);
}
Installation::SectorType Installation::Level::cell(std::uint32_t x, std::uint32_t y) const {
    return sectorAt(x,y).type;
}
std::size_t Installation::Level::interiorCount() const noexcept {
    std::size_t count=0;
    for (const auto& s:sectors_) if (s.type==SectorType::Interior) count+=s.area;
    return count;
}
std::size_t Installation::Level::horizontalWall(std::uint32_t x, std::uint32_t y) const {
    if (x>=width_ || y>depth_) throw std::out_of_range("Horizontal Wall outside Level");
    return std::size_t(y)*width_+x;
}
std::size_t Installation::Level::verticalWall(std::uint32_t x, std::uint32_t y) const {
    if (x>width_ || y>=depth_) throw std::out_of_range("Vertical Wall outside Level");
    return std::size_t(width_)*(std::size_t(depth_)+1)+std::size_t(y)*(std::size_t(width_)+1)+x;
}
std::pair<int,int> Installation::Level::wallCells(std::size_t wall) const {
    if (wall>=walls_.size()) throw std::out_of_range("Wall outside Level");
    const auto horizontal=std::size_t(width_)*(std::size_t(depth_)+1);
    if (wall<horizontal) {
        auto x=wall%width_, y=wall/width_;
        return {y ? int((y-1)*width_+x) : -1, y<depth_ ? int(y*width_+x) : -1};
    }
    wall-=horizontal;
    auto x=wall%(std::size_t(width_)+1), y=wall/(std::size_t(width_)+1);
    return {x ? int(y*width_+x-1) : -1, x<width_ ? int(y*width_+x) : -1};
}
Installation::WallType Installation::Level::eligibleWall(std::size_t wall) const {
    const auto [a,b]=wallCells(wall);
    auto inside=[&](int i) { return i>=0 && sectors_[cells_[i].sector].type==SectorType::Interior; };
    const bool ia=inside(a), ib=inside(b);
    return ia && ib ? WallType::Interior : ia || ib ? WallType::Exterior : WallType::None;
}
bool Installation::Level::canPlaceDoor(std::size_t wall) const {
    if (wall>=walls_.size() || eligibleWall(wall)==WallType::None) return false;
    auto solid=[&](std::size_t w) {
        return walls_[w].type==WallType::Interior || walls_[w].type==WallType::Exterior;
    };
    const auto horizontal=std::size_t(width_)*(std::size_t(depth_)+1);
    std::uint32_t x,y;
    bool alongX=wall<horizontal;
    if (alongX) {
        x=static_cast<std::uint32_t>(wall%width_); y=static_cast<std::uint32_t>(wall/width_);
        if (x==0 || x+1>=width_ || !solid(horizontalWall(x-1,y)) || !solid(horizontalWall(x+1,y))) return false;
    } else {
        const auto i=wall-horizontal;
        x=static_cast<std::uint32_t>(i%(std::size_t(width_)+1));
        y=static_cast<std::uint32_t>(i/(std::size_t(width_)+1));
        if (y==0 || y+1>=depth_ || !solid(verticalWall(x,y-1)) || !solid(verticalWall(x,y+1))) return false;
    }
    auto clearEndpoint=[&](std::uint32_t px,std::uint32_t py) {
        auto otherDoor=[&](std::size_t w) { return w!=wall && walls_[w].type==WallType::Door; };
        return !( (px>0 && otherDoor(horizontalWall(px-1,py)))
            || (px<width_ && otherDoor(horizontalWall(px,py)))
            || (py>0 && otherDoor(verticalWall(px,py-1)))
            || (py<depth_ && otherDoor(verticalWall(px,py))) );
    };
    return clearEndpoint(x,y) && clearEndpoint(x+(alongX ? 1 : 0),y+(alongX ? 0 : 1));
}
void Installation::Level::validateDoors() const {
    for (std::size_t w=0;w<walls_.size();++w)
        if (walls_[w].type==WallType::Door)
            require(canPlaceDoor(w),"Door requires solid Walls at both ends and no adjacent Door");
}
// Flood-fill same-type Cells across absent Walls. Preserve metadata by overlap:
// each old Sector gives its name only to its largest surviving fragment.
void Installation::Level::rebuild(const std::vector<SectorType>& types) {
    const auto oldSectors=sectors_;
    std::vector<int> oldIds;
    std::vector<std::size_t> oldFirst(oldSectors.size(),cells_.size());
    for (std::size_t i=0;i<cells_.size();++i) {
        oldIds.push_back(cells_[i].sector);
        oldFirst[cells_[i].sector]=std::min(oldFirst[cells_[i].sector],i);
    }
    std::vector<int> group(cells_.size(),-1);
    std::vector<std::vector<std::size_t>> components;
    for (std::size_t seed=0;seed<cells_.size();++seed) {
        if (group[seed]>=0) continue;
        int id=static_cast<int>(components.size());
        components.push_back({seed});
        group[seed]=id;
        for (std::size_t head=0;head<components.back().size();++head) {
            auto i=components.back()[head];
            auto x=static_cast<std::uint32_t>(i%width_), y=static_cast<std::uint32_t>(i/width_);
            auto visit=[&](std::size_t next,std::size_t wall) {
                if (group[next]<0 && types[next]==types[i] && walls_[wall].type==WallType::None) {
                    group[next]=id; components.back().push_back(next);
                }
            };
            if (x) visit(i-1,verticalWall(x,y));
            if (x+1<width_) visit(i+1,verticalWall(x+1,y));
            if (y) visit(i-width_,horizontalWall(x,y));
            if (y+1<depth_) visit(i+width_,horizontalWall(x,y+1));
        }
    }
    std::vector<std::map<int,std::size_t>> overlap(components.size());
    std::vector<int> largest(oldSectors.size(),-1);
    std::vector<std::size_t> largestSize(oldSectors.size(),0);
    for (std::size_t g=0;g<components.size();++g) {
        for (auto i:components[g])
            if (oldSectors[oldIds[i]].type==types[i]) ++overlap[g][oldIds[i]];
        for (auto [old,count]:overlap[g]) if (count>largestSize[old]) {
            largest[old]=static_cast<int>(g); largestSize[old]=count;
        }
    }
    // Reserve every old identity before allocating any new fragment. The
    // dominant fragment may be visited after a smaller, newly-created one.
    // New ids must also exceed all contributors so a later merge recovers
    // the pre-split identity via the lowest-dominated-uid rule.
    std::int64_t highestUid=0;
    for (const auto& old:oldSectors) highestUid=std::max(highestUid,old.uid);
    sectors_.clear();
    for (std::size_t g=0;g<components.size();++g) {
        const auto seed=components[g].front();
        Sector s{"",types[seed],1,components[g].size(),oldSectors.front().level};
        int donor=-1; std::size_t best=0;
        for (auto [old,count]:overlap[g])
            if (count>best || (count==best && donor>=0 && oldFirst[old]<oldFirst[donor])) {
                donor=old; best=count;
            }
        if (donor>=0) {
            s.height=oldSectors[donor].height;
            // The donor's starting condition carries to every fragment of it:
            // they are all the same kind of space. The name follows the largest
            // fragment only.
            s.atmosphere=oldSectors[donor].atmosphere;
            if (largest[donor]==int(g)) {
                s.name=oldSectors[donor].name;
                s.atmosphereAssignment=oldSectors[donor].atmosphereAssignment;
            }
        }
        if (s.type==SectorType::Void) { s.height=1; s.atmosphere=AtmospherePreset::Void; }
        // Identity: a fragment inherits the uid of a contributor it dominates,
        // taking the lowest such uid so that merging two Sectors returns the
        // survivor the identity the smaller of the pair had had before the split
        // - erase and its undo land on the same identity. A fragment that
        // dominates no contributor is new, and takes the next free uid.
        std::int64_t inherited=0;
        for (auto const& [old,count]:overlap[g]) if (largest[old]==int(g)) {
            auto const candidate=oldSectors[old].uid;
            if (candidate!=0 && (inherited==0 || candidate<inherited)) inherited=candidate;
        }
        s.uid=inherited;
        if (s.uid==0) {
            if (highestUid==std::numeric_limits<std::int64_t>::max())
                throw std::overflow_error("Sector uid space exhausted");
            s.uid=++highestUid;
        }
        sectors_.push_back(std::move(s));
        for (auto i:components[g]) cells_[i].sector=static_cast<int>(g);
    }
}
void Installation::Level::setCell(std::uint32_t x,std::uint32_t y,SectorType type) {
    auto i=index(x,y);
    require(type==SectorType::Void || type==SectorType::Interior,"Invalid Cell type");
    if (cell(x,y)==type) return;
    auto before=*this;
    std::vector<SectorType> types;
    for (const auto& c:cells_) types.push_back(sectors_[c.sector].type);
    types[i]=type;
    // Only the painted Cell's four edges can change classification. Outline
    // its new floor boundary before flood filling, so obsolete Exterior Walls
    // cannot accidentally partition a newly joined Interior region.
    for (const auto w : {horizontalWall(x,y), horizontalWall(x,y+1),
                         verticalWall(x,y), verticalWall(x+1,y)}) {
        const auto [a,b]=wallCells(w);
        const bool ia=a>=0 && types[a]==SectorType::Interior;
        const bool ib=b>=0 && types[b]==SectorType::Interior;
        if (walls_[w].type==WallType::Door) continue;
        if (ia != ib) walls_[w].type=WallType::Exterior;
        else if (!ia || walls_[w].type==WallType::Exterior) walls_[w].type=WallType::None;
        // A still-valid authored Interior Wall remains Interior.
    }
    try { rebuild(types); validateDoors(); }
    catch (...) { *this=std::move(before); throw; }
}
void Installation::Level::toggleWall(std::size_t wall) {
    auto& w=walls_.at(wall);
    w.type=w.type==WallType::None ? eligibleWall(wall) : WallType::None;
    std::vector<SectorType> types;
    for (const auto& c:cells_) types.push_back(sectors_[c.sector].type);
    rebuild(types);
}
Installation::Installation(std::uint32_t w,std::uint32_t d) : levels_{Level(w,d)} {}
Installation::Installation(std::vector<Level> levels) : levels_(std::move(levels)) { reindex(); validate(); }
void Installation::reindex() {
    require(levels_.size()<=INT_MAX,"Too many Levels");
    for (std::size_t i=0;i<levels_.size();++i) for (auto& s:levels_[i].sectors_) s.level=static_cast<int>(i);
}
void Installation::setCell(std::size_t l,std::uint32_t x,std::uint32_t y,SectorType type) {
    Installation next=*this; next.levels_.at(l).setCell(x,y,type); next.validate(); *this=std::move(next);
}
void Installation::toggleCell(std::size_t l,std::uint32_t x,std::uint32_t y) {
    setCell(l,x,y,level(l).cell(x,y)==SectorType::Interior ? SectorType::Void : SectorType::Interior);
}
void Installation::toggleWall(std::size_t l,std::size_t wall) {
    Installation next=*this; next.levels_.at(l).toggleWall(wall); next.validate(); *this=std::move(next);
}
void Installation::setDoor(std::size_t l,std::size_t wall,bool present) {
    Installation next=*this;
    auto& grid=next.levels_.at(l);
    auto& edge=grid.walls_.at(wall);
    if (present) {
        require(grid.canPlaceDoor(wall),"Door requires solid Walls at both ends and no adjacent Door");
        edge.type=WallType::Door;
        std::vector<SectorType> types;
        for (const auto& c:grid.cells_) types.push_back(grid.sectors_[c.sector].type);
        grid.rebuild(types);
    } else if (edge.type!=WallType::None) grid.toggleWall(wall);
    next.validate(); *this=std::move(next);
}
void Installation::setSector(std::size_t l,std::size_t sector,std::string name,int height) {
    Installation next=*this; auto& s=next.levels_.at(l).sectors_.at(sector);
    s.name=std::move(name); s.height=height; next.validate(); *this=std::move(next);
}
void Installation::setOffset(std::size_t l,int x,int y) {
    Installation next=*this; auto& grid=next.levels_.at(l); grid.offsetX_=x; grid.offsetY_=y;
    next.validate(); *this=std::move(next);
}
void Installation::addLevel(std::uint32_t w,std::uint32_t d,int x,int y) {
    Installation next=*this; next.levels_.emplace_back(w,d,SectorType::Void);
    next.levels_.back().offsetX_=x; next.levels_.back().offsetY_=y;
    next.reindex(); next.validate(); *this=std::move(next);
}
bool Installation::removeTopLevel() {
    require(levels_.size()>1,"Installation must retain at least one Level");
    bool claimed=false;
    for (std::size_t l=0;l+1<levels_.size();++l) for (const auto& s:levels_[l].sectors_)
        if (s.type==SectorType::Interior && l+std::size_t(s.height)==levels_.size()) claimed=true;
    if (claimed) {
        const auto& old=levels_.back(); Level cleared(old.width_,old.depth_,SectorType::Void);
        cleared.offsetX_=old.offsetX_; cleared.offsetY_=old.offsetY_; levels_.back()=std::move(cleared);
    } else levels_.pop_back();
    reindex(); return !claimed;
}
void Installation::validate() const {
    require(!levels_.empty(),"Installation must contain a Level");
    std::set<std::tuple<std::size_t,std::int64_t,std::int64_t>> claims;
    std::size_t compartments=0;
    for (std::size_t l=0;l<levels_.size();++l) {
        const auto& grid=levels_[l];
        grid.validateDoors();
        for (const auto& s:grid.sectors_) {
            if (s.type==SectorType::Interior && s.atmosphere!=AtmospherePreset::Void)
                ++compartments;
            require(s.height>=1,"Sector height must be positive");
            require(s.type!=SectorType::Void || s.height==1,"Void height must be 1");
            require(s.type!=SectorType::Interior || std::size_t(s.height)<=levels_.size()-l,
                    "Sector height extends above the highest Level");
            require(s.type!=SectorType::Void || s.atmosphere==AtmospherePreset::Void,
                    "Void Sector must carry the Void atmosphere");
            require(s.type!=SectorType::Void || s.atmosphereAssignment.inert(),
                    "Void Sector must carry no atmosphere loads");
        }
        for (std::size_t i=0;i<grid.cells_.size();++i) {
            const auto& s=grid.sectors_.at(grid.cells_[i].sector);
            if (s.type!=SectorType::Interior) continue;
            auto x=std::int64_t(grid.offsetX_)+static_cast<std::int64_t>(i%grid.width_);
            auto y=std::int64_t(grid.offsetY_)+static_cast<std::int64_t>(i/grid.width_);
            for (int h=0;h<s.height;++h)
                require(claims.emplace(l+std::size_t(h),x,y).second,"Sector volumes overlap");
        }
    }
    // Leave headroom below the simulation's measured agent instruction ceiling.
    constexpr std::size_t maxCompartments=128;
    require(compartments<=maxCompartments,
            "Installation has " + std::to_string(compartments)
            + " atmosphere compartments; the supported maximum is " + std::to_string(maxCompartments)
            + ". Reduce the number of Interior Sectors with non-Void atmosphere presets "
              "by merging compartments or using the Void atmosphere preset.");
}

Installation::HullSpec Installation::hullSpec(HullType type) {
    switch (type) {
    case HullType::BareSkin: return {8.0, 0.80, 0.40, 224.0};
    case HullType::StandardInsulated: return {0.6, 0.85, 0.20, 62.0};
    case HullType::MultiLayerBlanket: return {0.05, 0.03, 0.15, 3.8};
    }
    throw std::invalid_argument("Invalid Hull type");
}
Installation::PartitionSpec Installation::partitionSpec(PartitionType type) {
    switch (type) {
    case PartitionType::BarePartition: return {5.0};
    case PartitionType::ThermalBreak: return {1.0};
    case PartitionType::InsulatedPartition: return {0.3};
    }
    throw std::invalid_argument("Invalid Partition type");
}
void Installation::setPartitionType(std::optional<PartitionType> type) {
    if (type) (void)partitionSpec(*type);
    partitionType_ = type;
}
void Installation::setHullType(std::optional<HullType> type) {
    if (type) (void)hullSpec(*type);
    hullType_ = type;
}
const Installation::Sector* Installation::interiorAtWorld(
    std::size_t l, std::int64_t x, std::int64_t y) const {
    if (l >= levels_.size()) return nullptr;
    for (std::size_t base = 0; base <= l; ++base) {
        const auto& grid = levels_[base];
        // Compare before subtracting so even extreme query coordinates are safe.
        if (x < grid.offsetX_ || x >= std::int64_t(grid.offsetX_) + grid.width_
            || y < grid.offsetY_ || y >= std::int64_t(grid.offsetY_) + grid.depth_) continue;
        const auto& s = grid.sectorAt(static_cast<std::uint32_t>(x - grid.offsetX_),
                                     static_cast<std::uint32_t>(y - grid.offsetY_));
        if (s.type == SectorType::Interior && l - base < std::size_t(s.height)) return &s;
    }
    return nullptr;
}
Installation::ExteriorSkin Installation::exteriorSkin(std::size_t l, std::size_t sector) const {
    const auto& grid = levels_.at(l);
    const auto& s = grid.sectors_.at(sector);
    ExteriorSkin result;
    if (s.type == SectorType::Void) return result;
    result.sideM2 = sideExteriorAreaM2(l, sector);
    for (std::size_t i = 0; i < grid.cells_.size(); ++i) {
        if (grid.cells_[i].sector != int(sector)) continue;
        const auto x = std::int64_t(grid.offsetX_) + static_cast<std::int64_t>(i % grid.width_);
        const auto y = std::int64_t(grid.offsetY_) + static_cast<std::int64_t>(i / grid.width_);
        if (!interiorAtWorld(l + std::size_t(s.height), x, y))
            result.roofM2 += CellWidthM * CellDepthM;
        // A tall Sector below can touch this floor even though its own floor
        // starts several Levels down. Claimed volume, not just floor, covers it.
        if (l == 0 || !interiorAtWorld(l - 1, x, y))
            result.floorUndersideM2 += CellWidthM * CellDepthM;
    }
    return result;
}
double Installation::sideExteriorAreaM2(std::size_t l, std::size_t sector) const {
    const auto& grid = levels_.at(l);
    const auto& s = grid.sectors_.at(sector);
    if (s.type == SectorType::Void) return 0.0;
    double area = 0.0;
    for (std::size_t w = 0; w < grid.walls_.size(); ++w) {
        if (grid.walls_[w].type != WallType::Exterior
            && !(grid.walls_[w].type==WallType::Door && grid.eligibleWall(w)==WallType::Exterior)) continue;
        const auto [a, b] = grid.wallCells(w);
        if ((a >= 0 && grid.cells_[a].sector == int(sector))
            || (b >= 0 && grid.cells_[b].sector == int(sector))) {
            const bool horizontal = w < std::size_t(grid.width_) * (grid.depth_ + 1);
            area += (horizontal ? CellWidthM : CellDepthM) * LevelHeightM * s.height;
        }
    }
    return area;
}

std::string Installation::serialize() const {
    validate();
    YAML::Node root; root["version"]=4; root["name"]=name_;
    const char* hull = "none";
    if (hullType_) switch (*hullType_) {
    case HullType::BareSkin: hull = "BareSkin"; break;
    case HullType::StandardInsulated: hull = "StandardInsulated"; break;
    case HullType::MultiLayerBlanket: hull = "MultiLayerBlanket"; break;
    }
    root["hull"] = hull;
    const char* partition = "none";
    if (partitionType_) switch (*partitionType_) {
    case PartitionType::BarePartition: partition = "BarePartition"; break;
    case PartitionType::ThermalBreak: partition = "ThermalBreak"; break;
    case PartitionType::InsulatedPartition: partition = "InsulatedPartition"; break;
    }
    root["partition"] = partition;
    root["levels"]=YAML::Node(YAML::NodeType::Sequence);
    for (const auto& grid:levels_) {
        YAML::Node l; l["width"]=grid.width_; l["depth"]=grid.depth_;
        l["offset_x"]=grid.offsetX_; l["offset_y"]=grid.offsetY_;
        l["sectors"]=YAML::Node(YAML::NodeType::Sequence);
        for (const auto& s:grid.sectors_) {
            YAML::Node n; n["name"]=s.name; n["type"]=typeName(s.type); n["height"]=s.height;
            n["uid"]=s.uid; n["atmosphere"]=presetName(s.atmosphere);
            if (!s.atmosphereAssignment.inert()) {
                const auto& a=s.atmosphereAssignment;
                YAML::Node loads;
                loads["occupants"]=a.occupants;
                loads["oxygenUsePerOccupantKgPerS"]=a.oxygenUsePerOccupantKgPerS;
                loads["co2ProductionPerOccupantKgPerS"]=a.co2ProductionPerOccupantKgPerS;
                loads["sensibleHeatPerOccupantKW"]=a.sensibleHeatPerOccupantKW;
                loads["moistureAdditionPerOccupantKgPerS"]=a.moistureAdditionPerOccupantKgPerS;
                loads["leakRateKgPerS"]=a.leakRateKgPerS;
                if (!a.reservoirId.empty()) loads["reservoirId"]=a.reservoirId;
                loads["exchangerConductanceKWPerK"]=a.exchangerConductanceKWPerK;
                if (a.fire) {
                    YAML::Node f;
                    f["oxygenKgPerS"]=a.fire->oxygenKgPerS;
                    f["carbonMonoxideKgPerS"]=a.fire->carbonMonoxideKgPerS;
                    f["carbonDioxideKgPerS"]=a.fire->carbonDioxideKgPerS;
                    f["heatKW"]=a.fire->heatKW;
                    f["oxygenThresholdKg"]=a.fire->oxygenThresholdKg;
                    loads["fire"]=f;
                }
                n["atmosphereLoads"]=loads;
            }
            l["sectors"].push_back(n);
        }
        l["cells"]=YAML::Node(YAML::NodeType::Sequence);
        for (const auto& c:grid.cells_) {
            YAML::Node n; n["sector"]=c.sector;
            n["floor"]=YAML::Node(YAML::NodeType::Map);
            n["wiring"]=YAML::Node(YAML::NodeType::Map);
            n["piping"]=YAML::Node(YAML::NodeType::Map);
            l["cells"].push_back(n);
        }
        l["walls"]=YAML::Node(YAML::NodeType::Sequence);
        for (const auto& wall:grid.walls_) l["walls"].push_back(typeName(wall.type));
        root["levels"].push_back(l);
    }
    YAML::Emitter out; out<<root;
    if (!out.good()) throw serialization::SerializationException(out.GetLastError());
    return out.c_str();
}
void Installation::deserialize(const std::string& text) {
    try {
        auto documents=YAML::LoadAll(text); require(documents.size()==1,"Expected one YAML document");
        auto root=documents.front(); mapNode(root,"Installation");
        const int version = root["version"].as<int>();
        require(version == 3 || version == 4,"Unsupported Installation format");
        std::optional<HullType> hull;
        if (version == 4 && root["hull"]) {
            const auto value = root["hull"].as<std::string>();
            if (value == "BareSkin") hull = HullType::BareSkin;
            else if (value == "StandardInsulated") hull = HullType::StandardInsulated;
            else if (value == "MultiLayerBlanket") hull = HullType::MultiLayerBlanket;
            else require(value == "none", "Invalid Hull type");
        }
        std::optional<PartitionType> partition;
        if (version == 4 && root["partition"]) {
            const auto value = root["partition"].as<std::string>();
            if (value == "BarePartition") partition = PartitionType::BarePartition;
            else if (value == "ThermalBreak") partition = PartitionType::ThermalBreak;
            else if (value == "InsulatedPartition") partition = PartitionType::InsulatedPartition;
            else require(value == "none", "Invalid Partition type");
        }
        auto ls=root["levels"]; require(ls.IsSequence() && ls.size()>0,"Expected Levels");
        std::vector<Level> loaded;
        for (const auto& n:ls) {
            mapNode(n,"Level");
            Level grid(n["width"].as<std::uint32_t>(),n["depth"].as<std::uint32_t>(),SectorType::Void);
            grid.offsetX_=n["offset_x"].as<int>(); grid.offsetY_=n["offset_y"].as<int>();
            auto ss=n["sectors"]; require(ss.IsSequence() && ss.size()>0 && ss.size()<=grid.cells_.size(),"Invalid Sector count");
            grid.sectors_.clear();
            auto num=[](const YAML::Node& n,const char* key){ return n[key] ? n[key].as<double>() : 0.0; };
            for (const auto& sn:ss) {
                mapNode(sn,"Sector"); auto type=sn["type"].as<std::string>();
                require(type=="Void" || type=="Interior","Invalid Sector type");
                Sector sec;
                sec.name=sn["name"].as<std::string>();
                sec.type=type=="Void" ? SectorType::Void : SectorType::Interior;
                sec.height=sn["height"].as<int>();
                sec.level=int(loaded.size());
                if (sn["uid"]) sec.uid=sn["uid"].as<std::int64_t>();
                auto preset=sn["atmosphere"] ? sn["atmosphere"].as<std::string>()
                    : (sec.type==SectorType::Void ? "Void" : "Earth-like");
                require(preset=="Void" || preset=="Earth-like","Invalid Sector atmosphere preset");
                sec.atmosphere=preset=="Void" ? AtmospherePreset::Void : AtmospherePreset::EarthLike;
                if (sn["atmosphereLoads"]) {
                    auto ln=sn["atmosphereLoads"]; mapNode(ln,"atmosphereLoads");
                    auto& a=sec.atmosphereAssignment;
                    a.occupants=ln["occupants"] ? ln["occupants"].as<int>() : 0;
                    a.oxygenUsePerOccupantKgPerS=num(ln,"oxygenUsePerOccupantKgPerS");
                    a.co2ProductionPerOccupantKgPerS=num(ln,"co2ProductionPerOccupantKgPerS");
                    a.sensibleHeatPerOccupantKW=num(ln,"sensibleHeatPerOccupantKW");
                    a.moistureAdditionPerOccupantKgPerS=num(ln,"moistureAdditionPerOccupantKgPerS");
                    a.leakRateKgPerS=num(ln,"leakRateKgPerS");
                    a.reservoirId=ln["reservoirId"] ? ln["reservoirId"].as<std::string>() : std::string{};
                    a.exchangerConductanceKWPerK=num(ln,"exchangerConductanceKWPerK");
                    if (ln["fire"]) {
                        auto f=ln["fire"]; mapNode(f,"fire");
                        AtmosphereAssignment::Fire fire;
                        fire.oxygenKgPerS=num(f,"oxygenKgPerS");
                        fire.carbonMonoxideKgPerS=num(f,"carbonMonoxideKgPerS");
                        fire.carbonDioxideKgPerS=num(f,"carbonDioxideKgPerS");
                        fire.heatKW=num(f,"heatKW");
                        fire.oxygenThresholdKg=num(f,"oxygenThresholdKg");
                        a.fire=fire;
                    }
                }
                grid.sectors_.push_back(std::move(sec));
            }
            auto cs=n["cells"]; require(cs.IsSequence() && cs.size()==grid.cells_.size(),"Cell count does not match dimensions");
            std::vector<SectorType> types;
            for (std::size_t i=0;i<cs.size();++i) {
                auto cn=cs[i]; mapNode(cn,"Cell");
                for (const char* key:{"floor","wiring","piping"}) {
                    mapNode(cn[key],key); require(cn[key].size()==0,"Layer must be empty");
                }
                int id=cn["sector"].as<int>(); require(id>=0 && std::size_t(id)<ss.size(),"Invalid Cell Sector index");
                grid.cells_[i].sector=id; ++grid.sectors_[id].area; types.push_back(grid.sectors_[id].type);
            }
            for (const auto& s:grid.sectors_) require(s.area>0,"Empty Sector");
            auto ws=n["walls"]; require(ws.IsSequence() && ws.size()==grid.walls_.size(),"Invalid Wall count");
            for (std::size_t i=0;i<ws.size();++i) {
                auto t=ws[i].as<std::string>(); require(t=="None" || t=="Interior" || t=="Exterior" || t=="Door","Invalid Wall type");
                auto type=t=="None" ? WallType::None : t=="Interior" ? WallType::Interior : t=="Door" ? WallType::Door : WallType::Exterior;
                require(type==WallType::None || type==grid.eligibleWall(i)
                    || (type==WallType::Door && grid.eligibleWall(i)!=WallType::None),"Wall classification conflicts with Cells");
                grid.walls_[i].type=type;
            }
            // Verify topology without altering serialized Sector ordering/metadata.
            auto rebuilt=grid; rebuilt.rebuild(types);
            std::map<int,int> forward,reverse;
            for (std::size_t i=0;i<grid.cells_.size();++i) {
                int a=grid.cells_[i].sector,b=rebuilt.cells_[i].sector;
                require(!forward.contains(a) || forward[a]==b,"Disconnected Sector");
                require(!reverse.contains(b) || reverse[b]==a,"Unseparated Sectors");
                forward[a]=b; reverse[b]=a;
            }
            loaded.push_back(std::move(grid));
        }
        Installation next(std::move(loaded));
        next.name_=root["name"].as<std::string>();
        next.hullType_ = hull; // Never apply creation-time defaults when loading.
        next.partitionType_ = partition;
        *this=std::move(next);
    } catch (const std::exception& e) { throw serialization::SerializationException(e.what()); }
}
} // namespace nsi::core
