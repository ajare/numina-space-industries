#include "station/editor/Deck.h"

#include <stdexcept>

namespace station::editor {

Deck::Deck(std::uint32_t width, std::uint32_t height, CellState initial_state)
    : mWidth(width),
      mHeight(height),
      mCells(static_cast<std::size_t>(width) * height, initial_state) {}

bool Deck::contains(std::uint32_t x, std::uint32_t y) const noexcept {
    return x < mWidth && y < mHeight;
}

std::size_t Deck::index(std::uint32_t x, std::uint32_t y) const {
    if (!contains(x, y)) {
        throw std::out_of_range("Cell coordinate is outside the Deck");
    }
    return static_cast<std::size_t>(y) * mWidth + x;
}

CellState Deck::cell(std::uint32_t x, std::uint32_t y) const {
    return mCells.at(index(x, y));
}

void Deck::setCell(std::uint32_t x, std::uint32_t y, CellState state) {
    mCells.at(index(x, y)) = state;
}

std::size_t Deck::filledCount() const noexcept {
    std::size_t filled = 0;
    for (const CellState state : mCells) {
        if (state == CellState::Filled) {
            ++filled;
        }
    }
    return filled;
}

}  // namespace station::editor
