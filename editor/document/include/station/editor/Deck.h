// One rectangular level of a Space Station: a grid of Cells.
//
// Cells are addressed with the origin at (0, 0); x runs across the width and
// y down the height. Filled Cells need not form a connected region and a
// Deck may be entirely Void, so no mutation here validates geometry beyond
// the coordinate bounds.
#pragma once

#include "station/editor/CellState.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace station::editor {

class Deck {
public:
    // A width x height grid with every Cell in `initial_state`.
    Deck(std::uint32_t width, std::uint32_t height,
         CellState initial_state = CellState::Filled);

    std::uint32_t width() const noexcept { return mWidth; }
    std::uint32_t height() const noexcept { return mHeight; }
    std::size_t cellCount() const noexcept { return mCells.size(); }

    bool contains(std::uint32_t x, std::uint32_t y) const noexcept;

    // Throw std::out_of_range when the coordinate is outside the grid.
    CellState cell(std::uint32_t x, std::uint32_t y) const;
    void setCell(std::uint32_t x, std::uint32_t y, CellState state);

    std::size_t filledCount() const noexcept;
    bool allVoid() const noexcept { return filledCount() == 0; }

    bool operator==(const Deck&) const = default;

private:
    std::size_t index(std::uint32_t x, std::uint32_t y) const;

    std::uint32_t mWidth;
    std::uint32_t mHeight;
    std::vector<CellState> mCells;
};

}  // namespace station::editor
