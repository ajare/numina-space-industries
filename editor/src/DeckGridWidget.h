// Top-down, clickable view of a Deck for the Editor panel.
//
// Draws every Cell with a visually distinct Filled/Void appearance and
// reports the Cell under a left click, so the host can turn that into a
// session command. Input goes through an InvisibleButton so the widget owns
// its own clicks and never steals gestures from the rest of the Editor.
#pragma once

#include "station/editor/Deck.h"

#include <cstdint>
#include <optional>

struct DeckCellCoord {
    std::uint32_t x;
    std::uint32_t y;
};

// Returns the Cell toggled by a left click this frame, or nullopt when the
// click landed outside the Deck.
std::optional<DeckCellCoord> drawDeckGrid(const char* id,
                                         const station::editor::Deck& deck);
