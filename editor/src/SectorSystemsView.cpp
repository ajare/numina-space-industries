#include "SectorSystemsView.h"
#include "imnodes.h"

#include <algorithm>

namespace station::editor {
namespace {
// Length-prefixing avoids collisions even when authored ids contain delimiters.
std::string part(const std::string& value) { return std::to_string(value.size()) + ":" + value; }
std::string nodeKey(const std::string& type, const std::string& id) { return part(type) + part(id); }
std::string pinKey(const station::StationPinRef& port) {
    return nodeKey(port.nodeType, port.nodeId) + part(port.portName);
}
ImU32 portColor(const std::string& kind) {
    if (kind.find("heat") != std::string::npos || kind.find("thermal") != std::string::npos)
        return IM_COL32(255, 165, 60, 255);
    if (kind.find("biolog") != std::string::npos) return IM_COL32(100, 210, 135, 255);
    return IM_COL32(100, 180, 240, 255);
}
}
SectorSystemsView::~SectorSystemsView() { reset(); }
void SectorSystemsView::reset() {
    if (context_) ImNodes::DestroyContext(context_);
    context_ = nullptr;
    identity_.clear(); ids_.clear(); portPositions_.clear(); nodePositions_.clear();
    renderedLinkCount_ = 0; linkCreationStarted_ = false;
}
int SectorSystemsView::idFor(const std::string& key) {
    auto [it, inserted] = ids_.try_emplace(key, static_cast<int>(ids_.size() + 1));
    (void)inserted;
    return it->second;
}
std::optional<ImVec2> SectorSystemsView::portPosition(const station::StationPinRef& port) const {
    const auto it = portPositions_.find(pinKey(port));
    return it == portPositions_.end() ? std::nullopt : std::optional(it->second);
}
std::optional<ImVec2> SectorSystemsView::nodePosition(const std::string& type, const std::string& id) const {
    const auto it = nodePositions_.find(nodeKey(type, id));
    return it == nodePositions_.end() ? std::nullopt : std::optional(it->second);
}

void SectorSystemsView::draw(const SectorSystemsGraph& graph) {
    if (identity_ != graph.identity) { reset(); identity_ = graph.identity; }
    portPositions_.clear(); nodePositions_.clear();
    renderedLinkCount_ = 0; linkCreationStarted_ = false;
    ImGui::TextUnformatted(graph.title.c_str());
    ImGui::TextDisabled("Read-only configuration | Drag nodes to arrange | Middle/right mouse: pan");
    if (!graph.message.empty()) ImGui::TextWrapped("%s", graph.message.c_str());
    if (graph.nodes.empty()) return;

    auto* previous = ImNodes::GetCurrentContext();
    if (!context_) context_ = ImNodes::CreateContext();
    ImNodes::SetCurrentContext(context_);
    struct RestoreContext {
        ImNodesContext* previous;
        ~RestoreContext() { ImNodes::SetCurrentContext(previous); }
    } restore{previous};
    auto& io = ImNodes::GetIO();
    io.LinkCreationEnabled = false;
    io.LinkDetachWithModifierClick.Modifier = nullptr;
    io.AltMouseButton = ImGui::GetIO().MouseDown[ImGuiMouseButton_Right]
        ? ImGuiMouseButton_Right : ImGuiMouseButton_Middle;
    if (ImGui::SmallButton("Center view")) ImNodes::EditorContextResetPanning(ImVec2(0, 0));

    ImNodes::BeginNodeEditor();
    std::map<std::string, ImU32> drawnPins;
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        const auto& node = graph.nodes[index];
        const auto key = nodeKey(node.type, node.id);
        const bool firstAppearance = !ids_.contains("node" + key);
        const int nodeId = idFor("node" + key);
        // Seed layout only once per node identity. Subsequent frames retain
        // user placement in imnodes, never in the Installation or undo history.
        if (firstAppearance)
            ImNodes::SetNodeGridSpacePos(nodeId, ImVec2(30 + 510 * static_cast<float>(index % 2),
                                                       30 + 720 * static_cast<float>(index / 2)));
        if (node.unresolved) ImNodes::PushColorStyle(ImNodesCol_NodeOutline, IM_COL32(240, 120, 70, 255));
        ImNodes::BeginNode(nodeId);
        ImNodes::BeginNodeTitleBar();
        ImGui::Text("%s%s", node.type.c_str(), node.unresolved ? " (unresolved)" : "");
        ImGui::TextUnformatted(node.id.c_str());
        ImNodes::EndNodeTitleBar();
        ImGui::PushID(nodeId);
        ImGui::Dummy(ImVec2(440, 0));
        // A group/Dummy does not bound ImGui's WorkRect: framed headers
        // otherwise stretch across the entire node-editor canvas. A single
        // fixed-width table provides a local content region (and wrapping)
        // without borrowing or mutating ImGui's internal window rectangles.
        if (ImGui::BeginTable("NodeContent", 1, ImGuiTableFlags_SizingStretchSame, ImVec2(440, 0))) {
            ImGui::TableNextColumn();
            const auto summaryCount = std::min(std::size_t{3}, node.configuration.size());
            auto field = [&](const auto& value) { ImGui::TextWrapped("%s: %s", value.first.c_str(), value.second.c_str()); };
            for (std::size_t i = 0; i < summaryCount; ++i) field(node.configuration[i]);
            if (node.configuration.size() > summaryCount && ImGui::CollapsingHeader("Configuration"))
                for (std::size_t i = summaryCount; i < node.configuration.size(); ++i) field(node.configuration[i]);
            if (!node.simulationStatus.empty() &&
                ImGui::CollapsingHeader("Simulation state", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("%s", node.simulationStatus.c_str());
                for (const auto& value : node.simulation) field(value);
            }
            ImGui::Separator();
            ImGui::EndTable();
        }

        std::vector<const SectorSystemPort*> inputs, outputs;
        for (const auto& port : node.ports)
            (port.direction == station::StationPortDirection::Input ? inputs : outputs).push_back(&port);
        std::map<std::string, std::pair<bool, float>> pinRows;
        const float startX = ImGui::GetCursorPosX();
        auto port = [&](const SectorSystemPort& p, bool input) {
            const auto pin = pinKey(p.reference);
            const int attribute = idFor("pin" + pin);
            ImNodes::PushColorStyle(ImNodesCol_Pin, portColor(p.kind));
            ImNodes::PushColorStyle(ImNodesCol_PinHovered, portColor(p.kind));
            if (input) ImNodes::BeginInputAttribute(attribute);
            else ImNodes::BeginOutputAttribute(attribute);
            const float y = ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5f;
            ImGui::TextUnformatted(p.reference.portName.c_str());
            if (input) ImNodes::EndInputAttribute();
            else ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle(); ImNodes::PopColorStyle();
            drawnPins.emplace(pin, portColor(p.kind)); pinRows.emplace(pin, std::pair{input, y});
        };
        for (std::size_t row = 0; row < std::max(inputs.size(), outputs.size()); ++row) {
            const float y = ImGui::GetCursorPosY();
            if (row < inputs.size()) port(*inputs[row], true);
            else ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight()));
            if (row < outputs.size()) {
                ImGui::SameLine(); ImGui::SetCursorPos(ImVec2(startX + 240, y));
                port(*outputs[row], false);
            }
        }
        ImGui::PopID();
        ImNodes::EndNode();
        ImNodes::SetNodeDraggable(nodeId, true);
        const auto top = ImNodes::GetNodeScreenSpacePos(nodeId);
        const auto size = ImNodes::GetNodeDimensions(nodeId);
        nodePositions_[key] = top;
        for (const auto& [pin, row] : pinRows)
            portPositions_[pin] = ImVec2(row.first ? top.x : top.x + size.x, row.second);
        if (node.unresolved) ImNodes::PopColorStyle();
    }
    for (const auto& link : graph.links) {
        const auto from = pinKey(link.from), to = pinKey(link.to);
        if (!drawnPins.contains(from) || !drawnPins.contains(to)) continue;
        ImNodes::PushColorStyle(ImNodesCol_Link, drawnPins.at(from));
        ImNodes::Link(idFor("link" + part(link.identity)), idFor("pin" + from), idFor("pin" + to));
        ImNodes::PopColorStyle();
        ++renderedLinkCount_;
    }
    ImNodes::EndNodeEditor();
    int ignored;
    linkCreationStarted_ = ImNodes::IsLinkStarted(&ignored);
    // No create/detach/delete/context-menu handlers: even transient pin-drag
    // creation is disabled above. Node placement, selection and panning are
    // view state only.
}
} // namespace station::editor
