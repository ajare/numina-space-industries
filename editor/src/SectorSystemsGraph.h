#pragma once

#include <nsi/core/Installation.h>
#include <station/StationPorts.h>
#include <station/StationSystems.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace station::editor {
struct SectorSystemPort {
    station::StationPinRef reference;
    station::StationPortDirection direction;
    bool optional = false;
    std::string kind;
};
struct SectorSystemNode {
    std::string type, id;
    bool unresolved = false;
    std::vector<std::pair<std::string, std::string>> configuration;
    // Separate from authored values. A non-empty status also identifies a
    // component with unavailable telemetry; never fabricate zeros in that case.
    std::string simulationStatus;
    std::vector<std::pair<std::string, std::string>> simulation;
    std::vector<SectorSystemPort> ports;
};
struct SectorSystemsGraph {
    std::string identity, title, message;
    std::vector<SectorSystemNode> nodes;
    std::vector<station::AuthoredLink> links;
};
// Pure read-only projection of configuration and an optional committed snapshot.
// Never drives a runtime or infers neighbour connections. The caller must ensure
// the snapshot matches the authored document; components are matched by stable id.
// Invalid/stale selections produce an empty graph with a message.
SectorSystemsGraph buildSectorSystemsGraph(const nsi::core::Installation& installation,
                                          std::size_t level, std::size_t sector,
                                          const station::StationSystemsSnapshot* snapshot = nullptr);
} // namespace station::editor
