#pragma once

#include <cstddef>
#include <cstdint>
#include <nsi/core/Units.h>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace nsi::core {

class Installation {
public:
    struct FloorLayer { bool operator==(const FloorLayer&) const = default; };
    struct WiringLayer { bool operator==(const WiringLayer&) const = default; };
    struct PipingLayer { bool operator==(const PipingLayer&) const = default; };
    enum class SectorType { Void, Interior };
    enum class WallType { None, Interior, Exterior, Door };
    enum class HullType { BareSkin, StandardInsulated, MultiLayerBlanket };
    // Jointly calibrated provisional engineering assumptions; see ADR-0002.
    struct HullSpec {
        double transmittanceWPerM2K;
        double emissivity;
        double absorptivity;
        double calibratedFluxWPerM2;
    };
    static HullSpec hullSpec(HullType type);
    enum class PartitionType { BarePartition, ThermalBreak, InsulatedPartition };
    struct PartitionSpec { double transmittanceWPerM2K; };
    static PartitionSpec partitionSpec(PartitionType type);
    // The authored sky. Pure intensive authored data - irradiance, a period,
    // a radius, a reflectivity, a Beta angle and a phase offset - with no
    // temperatures, no runtime inventories and no solver state, exactly as
    // the Hull's radiative properties already are. The planet's rotation is
    // deliberately absent: it drives neither eclipse nor exposure
    // (ADR-0008). Its infrared temperature is likewise absent, because it
    // is derived from irradiance and Albedo by energy balance rather than
    // typed: a dimmer star then cools the planet on its own.
    struct Orbit {
        double stellarIrradianceWPerM2{1361.0}; // Full Sun at Earth
        double orbitalPeriodS{5570.0};          // Inertial period about the planet
        double radiusFromPlanetCentreM{6791000.0}; // ISS-like altitude
        // The fraction of incident stellar light the planet reflects, in
        // [0, 1]. Earth-like. It is the only authored half of Planet shine:
        // the planet's infrared is derived from it by energy balance, so a
        // reflective world both throws back more sunlight and glows less.
        double albedo{0.3};
        // Beta angle: the angle between the star's direction and the
        // orbital plane, in radians, on the open interval (-pi/2, pi/2).
        // Zero is coplanar - the deepest eclipse the model can hand out, so
        // it is the default and the author relaxes it deliberately. Growing
        // Beta both shallows the eclipse and tilts direct sunlight onto the
        // cross-track hull faces; past the radius-dependent critical angle
        // there is no eclipse at all. The pole itself is refused: a star
        // sitting on the orbit normal leaves no in-plane sweep to make.
        double betaAngleRad{0.0};
        // Shifts the whole exposure profile along the orbit in seconds, so
        // a run can open part-way through an eclipse instead of always at
        // Local noon. It moves when things happen, never what happens: the
        // eclipse fraction, the lit arc and every flux are untouched.
        // Zero keeps t = 0 at Local noon. Any finite value is legal - the
        // evaluator folds it into the cycle.
        double phaseOffsetS{0.0};
        // Throws std::invalid_argument for numbers no sky can be built from.
        void validate() const;
        bool operator==(const Orbit&) const = default;
    };
    // The starting condition of a Sector's air, referenced rather than stored.
    // A preset carries only intensive constants - composition fractions,
    // pressure, temperature - so Sectors of any volume share one definition and
    // the extensive quantities (gas masses, thermal capacity) are derived from
    // the preset and the Sector's volume at expansion time, and can never go
    // stale when the Sector is resized. Void is the absence of a compartment:
    // a Void Sector claims no volume and can hold no air.
    enum class AtmospherePreset { Void, EarthLike };
    // The authored per-compartment loads and couplings that no intensive preset
    // can carry: occupancy is a count rather than a density, a leak is a
    // total-mass flow rather than a fraction, and a thermal coupling names one
    // specific reservoir. Defaults are inert, so a Sector that only names a
    // preset still expands into a valid compartment.
    //
    // Without a Hull, inert assignments remain adiabatic. With a Hull the
    // bridge may additionally author geometry-derived conduction (ADR-0002).
    // This never implies a gas pathway.
    struct AtmosphereAssignment {
        int occupants = 0;
        double oxygenUsePerOccupantKgPerS = 0.0;
        double co2ProductionPerOccupantKgPerS = 0.0;
        double sensibleHeatPerOccupantKW = 0.0;
        double moistureAdditionPerOccupantKgPerS = 0.0;
        double leakRateKgPerS = 0.0;
        std::string reservoirId;  // empty: adiabatic, no thermal coupling
        double exchangerConductanceKWPerK = 0.0;
        struct Fire {
            double oxygenKgPerS = 0.0;
            double carbonMonoxideKgPerS = 0.0;
            double carbonDioxideKgPerS = 0.0;
            double heatKW = 0.0;
            double oxygenThresholdKg = 0.0;
            bool operator==(const Fire&) const = default;
        };
        std::optional<Fire> fire{};
        // True when nothing is assigned, so the whole block can be left out of a
        // document rather than written as zeros.
        bool inert() const noexcept {
            return occupants == 0 && oxygenUsePerOccupantKgPerS == 0.0
                && co2ProductionPerOccupantKgPerS == 0.0 && sensibleHeatPerOccupantKW == 0.0
                && moistureAdditionPerOccupantKgPerS == 0.0 && leakRateKgPerS == 0.0
                && reservoirId.empty() && exchangerConductanceKWPerK == 0.0 && !fire;
        }
        bool operator==(const AtmosphereAssignment&) const = default;
    };
    struct Cell {
        FloorLayer floor;
        WiringLayer wiring;
        PipingLayer piping;
        int sector = 0;
        bool operator==(const Cell&) const = default;
    };
    struct Sector {
        std::string name;
        SectorType type = SectorType::Interior;
        int height = 1;
        std::size_t area = 0;
        int level = 0;
        // Stable identity within the Level: assigned once and carried across
        // rebuilds by the largest surviving fragment, so references from the
        // systems bridge (atmosphere ids, pathways, monitors) survive Cell and
        // Wall edits that reshuffle Sector indices. 0 means unassigned.
        std::int64_t uid = 0;
        AtmospherePreset atmosphere = AtmospherePreset::EarthLike;
        AtmosphereAssignment atmosphereAssignment{};
        // Enclosed volume in cubic metres. A Void Sector claims no volume, so
        // it reports zero and can never be given a pressurized compartment.
        double volumeM3() const noexcept {
            return type == SectorType::Void
                ? 0.0
                : static_cast<double>(area) * static_cast<double>(height) * CellVolumeM3;
        }
        bool operator==(const Sector&) const = default;
    };
    struct Wall {
        WallType type = WallType::None;
        bool operator==(const Wall&) const = default;
    };
    class Level {
    public:
        Level(std::uint32_t width, std::uint32_t depth,
              SectorType initial = SectorType::Interior);
        std::uint32_t width() const noexcept { return width_; }
        std::uint32_t depth() const noexcept { return depth_; }
        int offsetX() const noexcept { return offsetX_; }
        int offsetY() const noexcept { return offsetY_; }
        std::size_t cellCount() const noexcept { return cells_.size(); }
        std::size_t interiorCount() const noexcept;
        bool allVoid() const noexcept { return interiorCount() == 0; }
        bool contains(std::uint32_t x, std::uint32_t y) const noexcept;
        SectorType cell(std::uint32_t x, std::uint32_t y) const;
        const Cell& cellData(std::uint32_t x, std::uint32_t y) const;
        const Sector& sectorAt(std::uint32_t x, std::uint32_t y) const;
        std::span<const Cell> cells() const noexcept { return cells_; }
        std::span<const Sector> sectors() const noexcept { return sectors_; }
        std::span<const Wall> walls() const noexcept { return walls_; }
        // Horizontal edges first (width * (depth+1)), then vertical edges.
        std::size_t horizontalWall(std::uint32_t x, std::uint32_t y) const;
        std::size_t verticalWall(std::uint32_t x, std::uint32_t y) const;
        // Doors need solid collinear supports and cannot share an endpoint.
        bool canPlaceDoor(std::size_t wall) const;
        // Standalone Level editing; Installation editing also checks occupancy.
        void setCell(std::uint32_t x, std::uint32_t y, SectorType type);
        bool operator==(const Level&) const = default;
    private:
        friend class Installation;
        std::uint32_t width_, depth_;
        int offsetX_ = 0, offsetY_ = 0;
        std::vector<Cell> cells_;
        std::vector<Sector> sectors_;
        std::vector<Wall> walls_;
        std::size_t index(std::uint32_t x, std::uint32_t y) const;
        std::pair<int, int> wallCells(std::size_t wall) const;
        WallType eligibleWall(std::size_t wall) const;
        void rebuild(const std::vector<SectorType>& types);
        void toggleWall(std::size_t wall);
        void validateDoors() const;
    };

    explicit Installation(std::uint32_t width = 16, std::uint32_t depth = 16);
    explicit Installation(std::vector<Level> levels);
    static Installation createDefault() { return Installation(); }
    const std::string& getName() const noexcept { return name_; }
    void setName(std::string name) { name_ = std::move(name); }
    std::optional<HullType> hullType() const noexcept { return hullType_; }
    void setHullType(std::optional<HullType> type);
    std::optional<PartitionType> partitionType() const noexcept { return partitionType_; }
    void setPartitionType(std::optional<PartitionType> type);
    // A new Installation authors the ISS-like Orbit its fields already
    // carry, following the MultiLayerBlanket new-Installation default, so
    // the author sees day/night thermal cycling without typing anything.
    // Absent still means no orbit and never a substituted default: the sky
    // stays dark and the Installation absorbs no stellar energy. Loading
    // never applies this default - see deserialize.
    std::optional<Orbit> orbit() const noexcept { return orbit_; }
    void setOrbit(std::optional<Orbit> orbit);
    struct ExteriorSkin {
        double sideM2 = 0.0;
        double roofM2 = 0.0;
        double floorUndersideM2 = 0.0;
        double totalM2() const noexcept { return sideM2 + roofM2 + floorUndersideM2; }
    };
    // World coordinates are Cell positions plus Level offsets, not metres.
    // Returns the Interior Sector claiming this volume (including height claims),
    // or nullptr for void/outside the Installation. Valid until the document is modified.
    const Sector* interiorAtWorld(std::size_t level, std::int64_t x, std::int64_t y) const;
    ExteriorSkin exteriorSkin(std::size_t level, std::size_t sector) const;
    double sideExteriorAreaM2(std::size_t level, std::size_t sector) const;
    std::span<const Level> levels() const noexcept { return levels_; }
    std::size_t levelCount() const noexcept { return levels_.size(); }
    const Level& level(std::size_t index) const { return levels_.at(index); }
    void setCell(std::size_t level, std::uint32_t x, std::uint32_t y, SectorType type);
    void toggleCell(std::size_t level, std::uint32_t x, std::uint32_t y);
    void toggleWall(std::size_t level, std::size_t wall);
    void setDoor(std::size_t level, std::size_t wall, bool present);
    void setSector(std::size_t level, std::size_t sector, std::string name, int height);
    void setOffset(std::size_t level, int x, int y);
    void addLevel(std::uint32_t width, std::uint32_t depth, int x = 0, int y = 0);
    // Returns false when clearing instead of removing to preserve a height claim.
    bool removeTopLevel();
    void validate() const;
    std::string serialize() const;
    // Transactional: invalid documents throw SerializationException.
    void deserialize(const std::string& yaml);
    bool operator==(const Installation&) const = default;
private:
    std::string name_;
    std::optional<HullType> hullType_ = HullType::MultiLayerBlanket;
    std::optional<PartitionType> partitionType_ = PartitionType::InsulatedPartition;
    // The member initialisers above are the ISS-like sky: full Sun at
    // Earth, a 92.8 minute inertial period at ISS altitude, an Earth-like
    // Albedo and a coplanar star at Local noon. Authored on creation only.
    std::optional<Orbit> orbit_ = Orbit{};
    std::vector<Level> levels_;
    void reindex();
};
} // namespace nsi::core
