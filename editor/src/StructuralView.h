#pragma once
#include "CellPlaybackRuntime.h"
#include <array>
#include <limits>

namespace station::editor {
// Reset-local cavity handle. NaNs deliberately produce history gaps.
struct StructuralSample {
    double seconds{};
    double meanC{}, floorC{}, ceilingC{}, sidesC{}, minimumC{}, maximumC{};
};
StructuralSample structuralSample(double seconds,const nsi::systems::StructuralSelectionSummary&);
struct StructuralGraph {
    static constexpr std::size_t limit=16;
    std::array<nsi::systems::StructuralSurfaceObservation,limit> surfaces{};
    std::array<nsi::systems::StructuralContactObservation,limit> contacts{};
    std::array<nsi::systems::StructuralPortalObservation,limit> portals{};
    nsi::systems::StructuralQueryResult surfaceCount{},contactCount{},portalCount{};
};
// Only one page of selected Cell/Sector sides and one side's immediate contacts.
// Never expands neighbours recursively or copies the Installation graph.
StructuralGraph structuralGraph(const CellPlaybackRuntime&,nsi::systems::StructuralOwner,
    std::optional<std::size_t> cavity,bool sector,std::size_t page=0,std::size_t surface=0);
std::optional<std::size_t> structuralCavity(const CellPlaybackRuntime&,nsi::systems::StructuralOwner);
}
