// Top-down, clickable view of a Level for the Editor panel.
//
// Draws every Cell with a visually distinct Interior/Void appearance and
// reports the pointer phases over the grid, so the host can turn them into
// session gestures (a press starts a stroke, dragging paints, release
// commits it). Input goes through an InvisibleButton so the widget owns its
// own gestures and never steals them from the rest of the Editor.
#pragma once

#include "station/editor/Installation.h"
#include "InstallationRenderMode.h"
#include <span>

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
// empty when the pointer is off the Level — a drag may wander outside it.
enum class EditMode { Cell, Wall, Sector, Door };

struct DeckPointerEvent {
    DeckPointerPhase phase = DeckPointerPhase::None;
    std::optional<DeckCellCoord> cell;
    std::optional<std::size_t> wall;
    bool erase = false; // Right-click in Door mode.
    bool toggleDoor = false; // Shift+left-click on a hovered runtime Door.
};

DeckPointerEvent drawDeckGrid(const char* id, const station::editor::Level& level,
                              EditMode mode = EditMode::Cell,
                              std::optional<int> selectedSector = std::nullopt,
                              std::span<const station::editor::SectorColour> thermalColours = {},
                              bool doorControlsEnabled = false,
                              std::span<const std::size_t> openDoors = {});
