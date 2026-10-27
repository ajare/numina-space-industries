// Consumer-style behaviour tests for the headless Editor session seam.
//
// Drives user-level commands (launch, New, toggle) and asserts observable
// outcomes: document shape, Cell states, and refused edits. Checks are
// explicit runtime requirements so they stay active in Release builds,
// matching the repository's other consumer executables.

#include "station/editor/CellState.h"
#include "station/editor/Deck.h"
#include "station/editor/EditorSession.h"
#include "station/editor/SpaceStation.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using station::editor::CellState;
using station::editor::Deck;
using station::editor::EditorSession;
using station::editor::kDefaultDeckHeight;
using station::editor::kDefaultDeckWidth;
using station::editor::SpaceStation;

int g_checks = 0;

void require(bool value, const std::string& what) {
    ++g_checks;
    if (!value) {
        throw std::runtime_error("Editor session contract failed: " + what);
    }
}

template <typename Fn>
void requireThrows(Fn&& fn, const std::string& what) {
    ++g_checks;
    try {
        fn();
    } catch (const std::out_of_range&) {
        return;
    }
    throw std::runtime_error("Expected refusal but succeeded: " + what);
}

bool allCells(const Deck& deck, CellState expected) {
    for (std::uint32_t y = 0; y < deck.height(); ++y) {
        for (std::uint32_t x = 0; x < deck.width(); ++x) {
            if (deck.cell(x, y) != expected) {
                return false;
            }
        }
    }
    return true;
}

void voidEveryCell(EditorSession& session) {
    for (std::uint32_t y = 0; y < kDefaultDeckHeight; ++y) {
        for (std::uint32_t x = 0; x < kDefaultDeckWidth; ++x) {
            session.toggleCell(0, x, y);
        }
    }
}

// Launch creates exactly one 16x16 Deck with all 256 Cells Filled, and the
// Space Station -> Deck -> Cell hierarchy is what the session exposes.
void launchCreatesTheDefaultStation() {
    const EditorSession session;
    const SpaceStation& station = session.station();

    require(station.deckCount() == 1, "launch holds exactly one Deck");
    const Deck& deck = station.deck(0);
    require(deck.width() == kDefaultDeckWidth, "default Deck is 16 wide");
    require(deck.height() == kDefaultDeckHeight, "default Deck is 16 tall");
    require(deck.cellCount() == 256, "default Deck has 256 Cells");
    require(deck.filledCount() == 256, "default Deck is entirely Filled");
    require(allCells(deck, CellState::Filled), "every default Cell reads Filled");
    require(station == SpaceStation::createDefault(),
           "launch state equals the default station");
}

// File > New produces the same default document, discarding prior edits.
void newCreatesTheDefaultStation() {
    EditorSession session;
    session.toggleCell(0, 3, 4);
    require(session.station().deck(0).cell(3, 4) == CellState::Void,
            "setup edit is visible before New");

    session.newDocument();
    const Deck& deck = session.station().deck(0);
    require(session.station().deckCount() == 1, "New holds exactly one Deck");
    require(deck.filledCount() == 256, "New resets every Cell to Filled");
    require(session.station() == SpaceStation::createDefault(),
            "New equals a freshly created default station");
}

// Clicking a Filled Cell makes it Void; clicking a Void Cell makes it Filled.
void togglingFlipsBothDirections() {
    EditorSession session;

    session.toggleCell(0, 5, 7);
    require(session.station().deck(0).cell(5, 7) == CellState::Void,
            "Filled Cell becomes Void on toggle");
    require(session.station().deck(0).filledCount() == 255,
            "one Cell leaves the Filled set");

    session.toggleCell(0, 5, 7);
    require(session.station().deck(0).cell(5, 7) == CellState::Filled,
            "Void Cell becomes Filled on toggle");
    require(session.station().deck(0).filledCount() == 256,
            "toggling back restores the full Filled set");

    require(session.station().deck(0).cell(6, 7) == CellState::Filled,
            "toggling one Cell leaves its neighbour untouched");
}

// An entirely Void Deck stays a valid document that can still be edited.
void emptyGeometryStaysValid() {
    EditorSession session;
    voidEveryCell(session);

    const Deck& deck = session.station().deck(0);
    require(deck.allVoid(), "every Cell toggled to Void reports allVoid");
    require(deck.filledCount() == 0, "an empty Deck has no Filled Cells");
    require(allCells(deck, CellState::Void), "every Cell reads Void");
    require(session.station().deckCount() == 1,
            "an empty Deck is still the station's Deck");

    session.toggleCell(0, 0, 0);
    require(session.station().deck(0).cell(0, 0) == CellState::Filled,
            "an empty Deck still accepts new floor");
}

// Disconnected Filled regions are valid: no connectivity rule is applied.
void disconnectedGeometryStaysValid() {
    EditorSession session;
    voidEveryCell(session);

    session.toggleCell(0, 0, 0);
    session.toggleCell(0, 15, 15);
    session.toggleCell(0, 8, 2);

    const Deck& deck = session.station().deck(0);
    require(deck.filledCount() == 3, "three isolated Cells are Filled");
    require(deck.cell(0, 0) == CellState::Filled, "first island exists");
    require(deck.cell(15, 15) == CellState::Filled, "second island exists");
    require(deck.cell(8, 2) == CellState::Filled, "third island exists");
    require(deck.cell(1, 1) == CellState::Void,
            "space between islands stays Void");
}

// Unknown Decks and out-of-bounds coordinates are refused without touching
// the document.
void invalidTargetsAreRefused() {
    EditorSession session;

    requireThrows([&session] { session.toggleCell(1, 0, 0); },
                 "toggling a Deck the station does not have");
    requireThrows([&session] { session.toggleCell(0, kDefaultDeckWidth, 0); },
                 "toggling a column outside the Deck");
    requireThrows([&session] { session.toggleCell(0, 0, kDefaultDeckHeight); },
                 "toggling a row outside the Deck");
    requireThrows([&session] { session.station().deck(1); },
                 "reading a Deck the station does not have");

    require(session.station() == SpaceStation::createDefault(),
            "refused edits leave the document untouched");
}

}  // namespace

int main() {
    try {
        launchCreatesTheDefaultStation();
        newCreatesTheDefaultStation();
        togglingFlipsBothDirections();
        emptyGeometryStaysValid();
        disconnectedGeometryStaysValid();
        invalidTargetsAreRefused();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Editor session consumer passed " << g_checks << " checks\n";
    return 0;
}
