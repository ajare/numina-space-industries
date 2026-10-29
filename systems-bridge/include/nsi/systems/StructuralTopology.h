#pragma once
#include <nsi/core/ThermalPresets.h>
#include <station/ThermalBody.h>
#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <vector>

namespace nsi::systems {
// Integer world Cell coordinates; z is a Level boundary, not metres.
struct StructuralPoint {
    std::int64_t x{},y{},z{};
    auto operator<=>(const StructuralPoint&) const = default;
};
enum class StructuralPart { Panel, DoorFrame, DoorLeaf };
struct StructuralAssemblyId {
    StructuralPoint origin{};
    int axis{}; // normal: 0=x, 1=y, 2=z
    StructuralPart part{StructuralPart::Panel};
    auto operator<=>(const StructuralAssemblyId&) const = default;
};
struct StructuralSurfaceId {
    StructuralAssemblyId assembly{};
    unsigned side{}; // 0 faces negative axis, 1 positive; independent of Sector index
    auto operator<=>(const StructuralSurfaceId&) const = default;
};
struct StructuralOwner {
    // Physical source Cell of a height claim, not a mutable Sector index.
    std::size_t level{};
    std::int64_t worldX{},worldY{};
    auto operator<=>(const StructuralOwner&) const = default;
};
struct StructuralAssembly {
    StructuralAssemblyId id{};
    double areaM2{};
    core::ThermalAssemblySpec material{};
    // No owner means geometric exterior, NEVER an Interior Void preset.
    std::array<std::optional<StructuralOwner>,2> owners{};
    std::array<std::size_t,2> bodies{};
    StructuralSurfaceId surfaceId(unsigned side) const { return {id,side}; }
    // Preset Hull first/second mean inside/outside, not negative/positive.
    core::ThermalSurfaceSpec surfaceMaterial(unsigned side) const {
        const unsigned first=owners[0] ? 0 : 1;
        return side==first ? material.firstSide : material.secondSide;
    }
};
struct StructuralOpening {
    StructuralAssemblyId face{};
    std::array<std::optional<StructuralOwner>,2> owners{};
    double areaM2{}; // geometric area, no hydraulic Cd
    bool door{}; // closed at Reset; leaf/frame remain allocated when opened later
};
struct StructuralTopologyLimits {
    std::size_t maxClaimedCells{262144};
    std::size_t maxAssemblies{1048576};
    std::size_t maxContacts{8388608};
    std::size_t maxStorageBytes{1024ULL*1024*1024};
};
struct StructuralTopology {
    std::vector<StructuralAssembly> assemblies;
    std::vector<station::ThermalBody> bodies;
    std::vector<station::ThermalSolidContact> contacts;
    std::vector<StructuralOpening> openings;
    std::size_t predictedStorageBytes{}; // checked conservative payload + scratch bound
};
// Opt-in Reset-only candidate. No solver, gas construction or playback changes.
// Throws length_error with a structural (not gas-compartment) diagnostic on limits.
StructuralTopology expandStructuralTopology(const core::Installation&,
    const StructuralTopologyLimits& = {});
} // namespace nsi::systems
