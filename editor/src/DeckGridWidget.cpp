#include "DeckGridWidget.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kMaxCellPx = 34.0f;
constexpr float kMinCellPx = 8.0f;

ImU32 cellColour(station::editor::CellState state) {
    // Filled reads as a lit floor plate, Void as open space behind it.
    return state == station::editor::CellState::Filled
               ? IM_COL32(126, 168, 200, 255)
               : IM_COL32(26, 29, 34, 255);
}

// The Cell under a screen point, or nullopt when the point is outside the
// drawn grid.
std::optional<DeckCellCoord> cellAt(const ImVec2& origin, float cell,
                                   const station::editor::Deck& deck,
                                   const ImVec2& point) {
    if (point.x < origin.x || point.y < origin.y) {
        return std::nullopt;
    }
    const std::uint32_t x =
        static_cast<std::uint32_t>((point.x - origin.x) / cell);
    const std::uint32_t y =
        static_cast<std::uint32_t>((point.y - origin.y) / cell);
    if (!deck.contains(x, y)) {
        return std::nullopt;
    }
    return DeckCellCoord{x, y};
}

}  // namespace

DeckPointerEvent drawDeckGrid(const char* id,
                             const station::editor::Deck& deck) {
    DeckPointerEvent event;

    const float available_width = ImGui::GetContentRegionAvail().x;
    const float columns =
        static_cast<float>(std::max<std::uint32_t>(1, deck.width()));
    const float cell = std::clamp(std::floor(available_width / columns),
                                 kMinCellPx, kMaxCellPx);
    const ImVec2 grid_size(cell * static_cast<float>(deck.width()),
                          cell * static_cast<float>(deck.height()));

    ImGui::InvisibleButton(id, grid_size);

    const ImVec2 origin = ImGui::GetItemRectMin();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    for (std::uint32_t y = 0; y < deck.height(); ++y) {
        for (std::uint32_t x = 0; x < deck.width(); ++x) {
            const ImVec2 a(origin.x + cell * static_cast<float>(x),
                          origin.y + cell * static_cast<float>(y));
            const ImVec2 b(a.x + cell, a.y + cell);
            draw->AddRectFilled(a, b, cellColour(deck.cell(x, y)));
        }
    }

    const ImU32 grid_colour = IM_COL32(70, 78, 90, 255);
    for (std::uint32_t i = 0; i <= deck.width(); ++i) {
        const float x = origin.x + cell * static_cast<float>(i);
        draw->AddLine(ImVec2(x, origin.y),
                     ImVec2(x, origin.y + grid_size.y), grid_colour);
    }
    for (std::uint32_t i = 0; i <= deck.height(); ++i) {
        const float y = origin.y + cell * static_cast<float>(i);
        draw->AddLine(ImVec2(origin.x, y),
                     ImVec2(origin.x + grid_size.x, y), grid_colour);
    }
    draw->AddRect(origin, ImVec2(origin.x + grid_size.x, origin.y + grid_size.y),
                 ImGui::GetColorU32(ImGuiCol_Border));

    const ImGuiIO& io = ImGui::GetIO();
    const std::optional<DeckCellCoord> under =
        cellAt(origin, cell, deck, io.MousePos);

    if (ImGui::IsItemHovered() && under.has_value()) {
        const ImVec2 a(origin.x + cell * static_cast<float>(under->x),
                      origin.y + cell * static_cast<float>(under->y));
        draw->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell),
                           IM_COL32(255, 255, 255, 46));
    }

    // The press is reported on activation, the drag while this widget still
    // holds the mouse, and the release globally so a stroke always ends even
    // if the button came up after the pointer left the grid.
    if (ImGui::IsItemActivated()) {
        event.phase = DeckPointerPhase::Press;
        event.cell = under;
    } else if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        event.phase = DeckPointerPhase::Drag;
        event.cell = under;
    } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        event.phase = DeckPointerPhase::Release;
        event.cell = under;
    }

    const std::optional<DeckCellCoord> readout =
        ImGui::IsItemHovered() ? under : std::nullopt;
    if (readout.has_value()) {
        const station::editor::CellState state =
            deck.cell(readout->x, readout->y);
        ImGui::Text("Cell (%u, %u): %s", readout->x, readout->y,
                   state == station::editor::CellState::Filled ? "Filled"
                                                             : "Void");
    } else {
        ImGui::Text("Click to toggle, drag to paint (%zu Filled of %zu)",
                   deck.filledCount(), deck.cellCount());
    }

    return event;
}
