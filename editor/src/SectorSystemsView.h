#pragma once

#include "SectorSystemsGraph.h"
#include "imgui.h"
#include <map>

struct ImNodesContext;

namespace station::editor {
// Presentation only. The view receives a const projection and exposes no
// configuration or link editing commands. Dragging nodes changes only the
// transient view layout; middle/right mouse pans and the wheel zooms around
// the pointer. Initial framing and Fit view include all nodes.
class SectorSystemsView {
public:
    ~SectorSystemsView();
    SectorSystemsView() = default;
    SectorSystemsView(const SectorSystemsView&) = delete;
    SectorSystemsView& operator=(const SectorSystemsView&) = delete;
    void draw(const SectorSystemsGraph& graph);
    void reset();
    void fitToView() { fitPending_=true; }
    // Last rendered geometry for deterministic headless interaction checks.
    std::optional<ImVec2> portPosition(const station::StationPinRef& port) const;
    std::optional<ImVec2> nodePosition(const std::string& type, const std::string& id) const;
    std::optional<ImVec2> nodeDimensions(const std::string& type, const std::string& id) const;
    float zoom() const { return zoom_; }
    ImVec2 canvasOrigin() const { return canvasOrigin_; }
    ImVec2 canvasSize() const { return canvasSize_; }
    bool linkCreationStarted() const { return linkCreationStarted_; }
    std::size_t renderedLinkCount() const { return renderedLinkCount_; }
private:
    int idFor(const std::string& key);
    ImNodesContext* context_ = nullptr;
    std::string identity_;
    std::map<std::string, int> ids_;
    std::map<std::string, ImVec2> portPositions_, nodePositions_, nodeDimensions_, worldPositions_;
    float zoom_ = 1.0f;
    bool fitPending_ = true;
    ImVec2 canvasOrigin_{}, canvasSize_{};
    bool linkCreationStarted_ = false;
    std::size_t renderedLinkCount_ = 0;
};
} // namespace station::editor
