#pragma once

#include "SectorSystemsGraph.h"
#include "imgui.h"
#include <map>

struct ImNodesContext;

namespace station::editor {
// Presentation only. The view receives a const projection and exposes no
// configuration or link editing commands. Dragging nodes changes only the
// transient view layout; middle/right mouse pans.
class SectorSystemsView {
public:
    ~SectorSystemsView();
    SectorSystemsView() = default;
    SectorSystemsView(const SectorSystemsView&) = delete;
    SectorSystemsView& operator=(const SectorSystemsView&) = delete;
    void draw(const SectorSystemsGraph& graph);
    void reset();
    // Last rendered geometry for deterministic headless interaction checks.
    std::optional<ImVec2> portPosition(const station::StationPinRef& port) const;
    std::optional<ImVec2> nodePosition(const std::string& type, const std::string& id) const;
    bool linkCreationStarted() const { return linkCreationStarted_; }
    std::size_t renderedLinkCount() const { return renderedLinkCount_; }
private:
    int idFor(const std::string& key);
    ImNodesContext* context_ = nullptr;
    std::string identity_;
    std::map<std::string, int> ids_;
    std::map<std::string, ImVec2> portPositions_, nodePositions_;
    bool linkCreationStarted_ = false;
    std::size_t renderedLinkCount_ = 0;
};
} // namespace station::editor
