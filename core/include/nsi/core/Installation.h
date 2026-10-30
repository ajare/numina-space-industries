#pragma once

#include <cstddef>
#include <cstdint>
#include <nsi/core/CivilDate.h>
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
        // Numerical support limit, not a physical calibration: keeps derived
        // temperatures (~1e27 K), their fourth powers (~1e107 K^4), and
        // per-face flux/power arithmetic far below double overflow. Shared
        // by authored-data validation and the bridge's raw-record boundary.
        static constexpr double MaxStellarIrradianceWPerM2 = 1e100;
        // Supported interval: (0, MaxStellarIrradianceWPerM2], finite.
        double stellarIrradianceWPerM2{1361.0}; // Full Sun at Earth
        double orbitalPeriodS{5570.0};          // Inertial period about the planet
        double radiusFromPlanetCentreM{6791000.0}; // ISS-like altitude
        // The fraction of incident stellar light the planet reflects, in
        // [0, 1]. Earth-like. It is the only authored half of Planet shine:
        // the planet's infrared is derived from it by energy balance, so a
        // reflective world both throws back more sunlight and glows less.
        double albedo{0.3};
        // Axial tilt, in degrees. The Installation is taken to orbit the
        // planet's equator with the planet's lean held fixed and pointed at
        // the star - permanent solstice - so this number is the latitude the
        // star sits directly over, and that is exactly the angle between the
        // star and the Installation's orbital plane the exposure model needs.
        // Zero puts the star over the equator: the deepest eclipse the model
        // can hand out, so it is the default and the author relaxes it
        // deliberately. Growing the tilt shallows the eclipse and lifts
        // direct sunlight onto the cross-track hull faces; past the
        // radius-dependent critical tilt there is no eclipse at all. The
        // exact poles are refused: a star sitting on the orbit normal leaves
        // no in-plane sweep to make. Seasons are not modelled - the lean
        // never swings away from the star.
        static constexpr double MaxAxialTiltDegrees = 90.0;
        // Supported interval: (-90, 90) degrees, finite.
        double axialTiltDeg{0.0};
        // Where in the orbit cycle the run starts, as a percentage of the
        // Orbital period: 0% is Local noon, 50% is local midnight. It lets
        // a run open part-way through an eclipse instead of always at the
        // same point. It moves when things happen, never what happens: the
        // eclipse fraction, the lit arc and every flux are untouched.
        // Authored as a percentage so it stays a position on the cycle when
        // the Orbital period is edited, rather than a fixed number of
        // seconds that means a different place each time. Any finite value
        // is legal - the evaluator folds it, so 370% starts where 70% does
        // and -10% where 90% does.
        double phaseOffsetPercent{0.0};
        // The radius of the world being orbited, in metres. This model had
        // exactly one world before this field existed: core::PlanetRadiusM,
        // Earth-like and baked in. It is not decoration. It is the radius
        // of the shadow cylinder and the (Rp/r)^2 of the planet-shine view
        // factor, so a giant world drawn with an Earth-sized occluder is
        // not a slightly wrong sky but a different one - a low orbit about
        // a wide world is eclipsed almost continuously where the same
        // radius about Earth barely dips into shadow at all. Defaulting to
        // Earth keeps every record written before this field honest with no
        // migration: absent means the world they were actually flown about.
        // Must be positive and below the orbit radius - a Hull cannot sit
        // inside the ground it orbits.
        double planetRadiusM{PlanetRadiusM};
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
    // When the mission begins, as an instant: seconds since
    // 1970-01-01T00:00:00Z. Authored as a calendar date and never as an
    // hour of the day, so this is always an exact multiple of
    // kSecondsPerDay - the Editor's picker cannot produce anything else,
    // and validate() refuses a hand-written save that is not midnight
    // rather than quietly rounding it to one. The clock a run shows is
    // this plus simulated seconds; see CivilDate.h.
    std::int64_t startDateUtcS() const noexcept { return startDateUtcS_; }
    void setStartDateUtcS(std::int64_t seconds) noexcept { startDateUtcS_ = seconds; }
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
    // Albedo and no axial tilt - the star over the equator at Local noon.
    // Authored on creation only.
    std::optional<Orbit> orbit_ = Orbit{};
    // 1st January 2050, midnight UTC. A document that names no start
    // date reads as this, so a newly created Installation has a clock
    // without the author having to set one.
    std::int64_t startDateUtcS_ = kDefaultStartDateUtcS;
    std::vector<Level> levels_;
    void reindex();
};
} // namespace nsi::core
