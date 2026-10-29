#include "StructuralView.h"
#include <algorithm>

namespace station::editor {
StructuralSample structuralSample(double seconds,const nsi::systems::StructuralSelectionSummary& s) {
    const auto nan=std::numeric_limits<double>::quiet_NaN();
    auto c=[&](const auto& group) {auto k=group.areaWeightedTemperatureK();return k ? *k-273.15 : nan;};
    return {seconds,c(s.all),c(s.floor),c(s.ceiling),c(s.sides),
        s.all.surfaces ? s.all.minTemperatureK-273.15 : nan,
        s.all.surfaces ? s.all.maxTemperatureK-273.15 : nan};
}
std::optional<std::size_t> structuralCavity(const CellPlaybackRuntime& runtime,nsi::systems::StructuralOwner owner) {
    const auto& topology=runtime.model().geometry().structure;
    if (!topology) return std::nullopt;
    const auto& owners=topology->cavityOwners;
    const auto it=std::find_if(owners.begin(),owners.end(),
        [&](const auto& entry){return entry.first==owner;});
    return it!=owners.end() && it->first==owner ? std::optional(it->second) : std::nullopt;
}
StructuralGraph structuralGraph(const CellPlaybackRuntime& runtime,nsi::systems::StructuralOwner owner,
    std::optional<std::size_t> cavity,bool sector,std::size_t page,std::size_t surface) {
    StructuralGraph graph;
    const auto& model=runtime.model();
    if (page>std::numeric_limits<std::size_t>::max()/StructuralGraph::limit) return graph;
    graph.surfaceCount=sector && cavity ? model.structuralSectorSurfaces(*cavity,graph.surfaces,page*StructuralGraph::limit)
        : model.structuralSurfaces(owner,graph.surfaces,page*StructuralGraph::limit);
    if (surface<graph.surfaceCount.written)
        graph.contactCount=model.structuralContacts(graph.surfaces[surface].id,graph.contacts);
    if (cavity) graph.portalCount=model.structuralPortals(*cavity,graph.portals);
    return graph;
}
}
