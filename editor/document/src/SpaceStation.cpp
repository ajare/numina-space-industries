#include "station/editor/SpaceStation.h"

#include <stdexcept>
#include <utility>

namespace station::editor {

SpaceStation::SpaceStation(std::vector<Deck> decks)
    : mDecks(std::move(decks)) {}

SpaceStation SpaceStation::createDefault() {
    std::vector<Deck> decks;
    decks.emplace_back(kDefaultDeckWidth, kDefaultDeckHeight, CellState::Filled);
    return SpaceStation(std::move(decks));
}

const Deck& SpaceStation::deck(std::size_t index) const {
    if (index >= mDecks.size()) {
        throw std::out_of_range("Space Station has no Deck at that index");
    }
    return mDecks.at(index);
}

void SpaceStation::setCell(std::size_t deck_index, std::uint32_t x,
                          std::uint32_t y, CellState state) {
    if (deck_index >= mDecks.size()) {
        throw std::out_of_range("Space Station has no Deck at that index");
    }
    mDecks.at(deck_index).setCell(x, y, state);
}

void SpaceStation::toggleCell(std::size_t deck_index, std::uint32_t x,
                             std::uint32_t y) {
    const CellState state = deck(deck_index).cell(x, y);
    setCell(deck_index, x, y,
           state == CellState::Filled ? CellState::Void : CellState::Filled);
}

}  // namespace station::editor
