// Top-down, clickable view of a Deck for the Editor panel.
//
// Draws every Cell with a visually distinct Filled/Void appearance and
// reports the pointer phases over the grid, so the host can turn them into
// session gestures (a press starts a stroke, dragging paints, release
// commits it). Input goes through an InvisibleButton so the widget owns its
// own gestures and never steals them from the rest of the Editor.
#pragma once

#include "station/editor/Deck.h"

#include <cstdint>
#include <optional>

struct DeckCellCoord {
    std::uint32_t x;
    std::uint32_t y;
};

enum class DeckPointerPhase {
    None,     // nothing happened over the grid this frame
    Press,    // the left button went down on the grid: begin a stroke
    Drag,     // the left button is still held from that press: paint the Cell
    Release,  // the left button came up: finish the stroke
};

// The pointer event for this frame. `cell` is the Cell under the pointer,
// empty when the pointer is off the Deck — a drag may wander outside it.
struct DeckPointerEvent {
    DeckPointerPhase phase = DeckPointerPhase::None;
    std::optional<DeckCellCoord> cell;
};

DeckPointerEvent drawDeckGrid(const char* id, const station::editor::Deck& deck);
