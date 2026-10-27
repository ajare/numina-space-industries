// A Space Station: the stack of Decks the Editor authors.
//
// The domain models a station as at least one Deck, aligned at a shared
// origin so matching cell coordinates line up vertically. This milestone
// creates and edits exactly one 16x16 Deck, so the collection is exposed
// read-only: no add, remove, resize, or reorder surface exists yet.
#pragma once

#include "station/editor/Deck.h"

#include <cstddef>
#include <span>
#include <vector>

namespace station::editor {

class SpaceStation {
public:
    SpaceStation() = default;

    // Take ownership of an existing Deck collection.
    explicit SpaceStation(std::vector<Deck> decks);

    // The document a fresh launch or File > New produces: exactly one
    // 16x16 Deck with every Cell Filled.
    static SpaceStation createDefault();

    std::span<const Deck> decks() const noexcept { return mDecks; }
    std::size_t deckCount() const noexcept { return mDecks.size(); }

    // Throw std::out_of_range when the index has no Deck.
    const Deck& deck(std::size_t index) const;

    // Flip one Cell between Filled and Void. Cell-level editing is allowed;
    // the Deck collection itself (its size, order, and dimensions) is not.
    // Throw std::out_of_range for an unknown Deck or coordinate.
    void toggleCell(std::size_t deck_index, std::uint32_t x, std::uint32_t y);

    bool operator==(const SpaceStation&) const = default;

private:
    std::vector<Deck> mDecks;
};

}  // namespace station::editor
