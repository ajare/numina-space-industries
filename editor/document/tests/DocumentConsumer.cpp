// Consumer-style behaviour tests for the headless Editor session seam.
//
// Drives user-level commands (launch, New, click, drag stroke, undo, redo)
// and asserts observable outcomes: document shape, Cell states, gesture
// grouping, and history. Checks are explicit runtime requirements so they
// stay active in Release builds, matching the repository's other consumer
// executables.

#include "station/editor/Installation.h"
#include "station/editor/EditorSession.h"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using station::editor::SectorType;
using station::editor::Level;
using station::editor::EditorSession;
using station::editor::kDefaultLevelDepth;
using station::editor::kDefaultLevelWidth;
using station::editor::Installation;

int g_checks = 0;

struct CellPos {
    std::uint32_t x;
    std::uint32_t y;
};

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

bool allCells(const Level& level, SectorType expected) {
    for (std::uint32_t y = 0; y < level.depth(); ++y) {
        for (std::uint32_t x = 0; x < level.width(); ++x) {
            if (level.cell(x, y) != expected) {
                return false;
            }
        }
    }
    return true;
}

// One whole drag gesture: press on the first Cell, drag over the rest,
// release. The starting Cell is painted like any other Cell the stroke
// visits.
void applyGesture(EditorSession& session, std::size_t deck_index,
                 std::initializer_list<CellPos> cells) {
    if (cells.size() == 0) {
        return;
    }
    const CellPos start = *cells.begin();
    session.beginStroke(deck_index, start.x, start.y);
    for (const CellPos& cell : cells) {
        session.paintStroke(cell.x, cell.y);
    }
    session.endStroke();
}

void voidEveryCell(EditorSession& session) {
    for (std::uint32_t y = 0; y < kDefaultLevelDepth; ++y) {
        for (std::uint32_t x = 0; x < kDefaultLevelWidth; ++x) {
            session.toggleCell(0, x, y);
        }
    }
}

// Launch creates exactly one 16x16 Level with all 256 Cells Interior, and the
// Installation -> Level -> Cell hierarchy is what the session exposes.
void launchCreatesTheDefaultStation() {
    const EditorSession session;
    const Installation& station = session.station();

    require(station.levelCount() == 1, "launch holds exactly one Level");
    const Level& level = station.level(0);
    require(level.width() == kDefaultLevelWidth, "default Level is 16 wide");
    require(level.depth() == kDefaultLevelDepth, "default Level is 16 tall");
    require(level.cellCount() == 256, "default Level has 256 Cells");
    require(level.interiorCount() == 256, "default Level is entirely Interior");
    require(allCells(level, SectorType::Interior), "every default Cell reads Interior");
    require(station == Installation::createDefault(),
           "launch state equals the default station");
    require(!session.canUndo() && !session.canRedo(),
           "a fresh launch has no history");
}

// File > New produces the same default document, discarding prior edits.
void newCreatesTheDefaultStation() {
    EditorSession session;
    session.toggleCell(0, 3, 4);
    require(session.station().level(0).cell(3, 4) == SectorType::Void,
            "setup edit is visible before New");

    session.newDocument();
    const Level& level = session.station().level(0);
    require(session.station().levelCount() == 1, "New holds exactly one Level");
    require(level.interiorCount() == 256, "New resets every Cell to Interior");
    require(session.station() == Installation::createDefault(),
            "New equals a freshly created default station");
}

// Clicking a Interior Cell makes it Void; clicking a Void Cell makes it Interior.
void togglingFlipsBothDirections() {
    EditorSession session;

    session.toggleCell(0, 5, 7);
    require(session.station().level(0).cell(5, 7) == SectorType::Void,
            "Interior Cell becomes Void on toggle");
    require(session.station().level(0).interiorCount() == 255,
            "one Cell leaves the Interior set");

    session.toggleCell(0, 5, 7);
    require(session.station().level(0).cell(5, 7) == SectorType::Interior,
            "Void Cell becomes Interior on toggle");
    require(session.station().level(0).interiorCount() == 256,
            "toggling back restores the full Interior set");

    require(session.station().level(0).cell(6, 7) == SectorType::Interior,
            "toggling one Cell leaves its neighbour untouched");
}

// A drag that starts on a Interior Cell paints Void across every Cell it
// visits, and one that starts on a Void Cell paints Interior.
void strokeModeFollowsTheStartingCell() {
    EditorSession session;

    applyGesture(session, 0, {{2, 2}, {2, 3}, {2, 4}, {3, 4}});
    const Level& level = session.station().level(0);
    require(level.cell(2, 2) == SectorType::Void,
            "stroke from Interior voids its starting Cell");
    require(level.cell(2, 4) == SectorType::Void,
            "stroke from Interior voids a later Cell");
    require(level.cell(3, 4) == SectorType::Void,
            "stroke from Interior voids the last Cell");
    require(level.interiorCount() == 256 - 4,
            "stroke from Interior voids exactly the visited Cells");

    // Now the other mode: start on a Cell the previous stroke emptied.
    applyGesture(session, 0, {{2, 3}, {3, 3}, {4, 3}});
    const Level& after = session.station().level(0);
    require(after.cell(2, 3) == SectorType::Interior,
            "stroke from Void fills its starting Cell");
    require(after.cell(3, 3) == SectorType::Interior,
            "stroke from Void fills a Cell it drags over");
    require(after.cell(4, 3) == SectorType::Interior,
            "stroke from Void fills the last Cell");
    require(after.cell(2, 2) == SectorType::Void,
            "stroke from Void leaves untouched Cells alone");
}

// Revisiting a Cell inside one gesture sets it to the stroke's state again
// rather than toggling it back.
void strokeRevisitsDoNotToggle() {
    EditorSession session;

    // Start on Interior -> the stroke paints Void. Crossing the starting Cell
    // again must leave it Void, not Interior.
    session.beginStroke(0, 6, 6);
    session.paintStroke(6, 6);
    session.paintStroke(6, 7);
    session.paintStroke(6, 6);
    session.endStroke();
    const Level& level = session.station().level(0);
    require(level.cell(6, 6) == SectorType::Void,
            "revisiting the starting Cell keeps the stroke's Void state");
    require(level.cell(6, 7) == SectorType::Void, "dragged Cell is Void");
    require(level.interiorCount() == 256 - 2,
            "a revisit paints no extra Cells and reverts none");

    // A Cell already in the stroke's state contributes nothing.
    session.beginStroke(0, 9, 9);
    session.paintStroke(9, 9);
    session.paintStroke(6, 6);  // already Void
    session.endStroke();
    require(session.station().level(0).interiorCount() == 256 - 3,
            "painting an already-Void Cell changes only the new one");
}

// One gesture is one undo step however many Cells it touched.
void oneGestureIsOneUndoStep() {
    EditorSession session;

    applyGesture(session, 0, {{1, 1}, {1, 2}, {1, 3}, {2, 3}, {3, 3}});
    require(session.station().level(0).interiorCount() == 256 - 5,
            "five-Cell gesture voided five Cells");

    session.undo();
    require(session.station() == Installation::createDefault(),
            "one undo reversed the whole five-Cell gesture");
    require(!session.canUndo(),
            "undoing the only gesture leaves nothing to undo");
    require(session.canRedo(), "the reversed gesture is available to redo");

    session.redo();
    require(session.station().level(0).interiorCount() == 256 - 5,
            "one redo reapplied the whole five-Cell gesture");
    require(session.station().level(0).cell(3, 3) == SectorType::Void,
            "redo restored the last Cell of the gesture");
}

// A click is a one-Cell gesture and undoes as one step; several gestures
// undo and redo one at a time in order.
void clicksAndGesturesStepThroughHistoryInOrder() {
    EditorSession session;

    session.toggleCell(0, 0, 0);  // click 1: Void
    session.toggleCell(0, 1, 0);  // click 2: Void
    applyGesture(session, 0, {{2, 0}, {3, 0}});  // drag: Void

    require(session.station().level(0).interiorCount() == 256 - 4,
            "two clicks and a drag voided four Cells");

    session.undo();
    require(session.station().level(0).cell(2, 0) == SectorType::Interior &&
                session.station().level(0).cell(3, 0) == SectorType::Interior,
            "undo removed only the drag");
    require(session.station().level(0).cell(1, 0) == SectorType::Void,
            "undo left the earlier clicks in place");

    session.undo();
    require(session.station().level(0).cell(1, 0) == SectorType::Interior,
            "second undo removed the second click");

    session.redo();
    require(session.station().level(0).cell(1, 0) == SectorType::Void,
            "redo restored the second click");
    require(session.station().level(0).cell(2, 0) == SectorType::Interior,
            "redo did not run ahead to the drag");
}

// A new edit after an undo discards the abandoned redo branch.
void newEditInvalidatesRedoBranch() {
    EditorSession session;

    applyGesture(session, 0, {{4, 4}, {4, 5}});
    session.undo();
    require(session.canRedo(), "the undone gesture is redoable before a new edit");

    session.toggleCell(0, 12, 12);
    require(!session.canRedo(),
            "a new edit after undo drops the redo branch");

    session.redo();
    require(session.station().level(0).cell(4, 4) == SectorType::Interior &&
                session.station().level(0).cell(4, 5) == SectorType::Interior,
            "redo after invalidation restores nothing of the abandoned gesture");
    require(session.station().level(0).cell(12, 12) == SectorType::Void,
            "the new edit survives a refused redo");
}

// New clears history, so undo cannot restore another document's Cells.
void newDocumentClearsHistory() {
    EditorSession session;

    applyGesture(session, 0, {{7, 7}, {7, 8}, {7, 9}});
    session.newDocument();

    require(!session.canUndo() && !session.canRedo(),
            "New leaves no undo or redo history");
    session.undo();
    session.redo();
    require(session.station() == Installation::createDefault(),
            "undo and redo after New cannot restore the previous document");
}

// The seam a successful Open uses: adopt another document as a fresh
// baseline with no history carried across.
void replaceDocumentEstablishesAFreshBaseline() {
    EditorSession session;
    applyGesture(session, 0, {{0, 0}, {0, 1}});

    std::vector<Level> levels;
    levels.emplace_back(Level(kDefaultLevelWidth, kDefaultLevelDepth,
                           SectorType::Void));
    levels.back().setCell(3, 3, SectorType::Interior);
    const Installation loaded(std::move(levels));

    session.replaceDocument(loaded);
    require(session.station() == loaded,
            "replaceDocument adopts the given document exactly");
    require(!session.canUndo() && !session.canRedo(),
            "an adopted document starts with no history");

    session.undo();
    require(session.station() == loaded,
            "undo cannot pull the previous document's edits into the new one");

    session.toggleCell(0, 0, 0);
    require(session.station().level(0).cell(0, 0) == SectorType::Interior,
            "the adopted document stays editable");
    session.undo();
    require(session.station() == loaded,
            "undo after the adopted document returns to its baseline");
}

// A gesture that changes nothing leaves no history entry, so undo never
// steps over an empty gesture.
void emptyGestureLeavesNoHistoryEntry() {
    EditorSession session;

    // Pressed and released without painting anything.
    session.beginStroke(0, 5, 5);
    session.endStroke();
    require(!session.canUndo(),
            "a stroke that painted nothing records no undo step");

    voidEveryCell(session);
    session.toggleCell(0, 6, 6);  // (6, 6) back to Interior
    const Installation before = session.station();

    // Start on a Void Cell, so the stroke paints Interior, and visit only a
    // Cell that is already Interior.
    session.beginStroke(0, 0, 0);
    session.paintStroke(6, 6);
    session.endStroke();
    require(session.station() == before,
            "a stroke visiting only Cells already in its state changes nothing");

    session.undo();
    require(session.station().level(0).allVoid(),
            "one undo reverses the last real gesture and skips the empty one");
}

// Undo and redo with nothing to do are safe no-ops.
void historylessUndoAndRedoAreSafe() {
    EditorSession session;

    session.undo();
    session.redo();
    require(session.station() == Installation::createDefault(),
            "undo/redo with no history changes nothing");
    require(!session.canUndo() && !session.canRedo(),
            "still no history after refused undo/redo");
}

// Painting outside the stroked Level is ignored, not an error: a drag can
// wander off the grid.
void paintingOffTheDeckIsIgnored() {
    EditorSession session;

    session.beginStroke(0, 0, 0);
    session.paintStroke(0, 0);
    session.paintStroke(kDefaultLevelWidth, 0);
    session.paintStroke(0, kDefaultLevelDepth);
    session.paintStroke(99, 99);
    session.endStroke();

    require(session.station().level(0).interiorCount() == 255,
            "only the starting Cell was painted, off-Level visits ignored");
    require(session.station().level(0).cell(0, 0) == SectorType::Void,
            "the starting Cell still took the stroke's state");
}

// Unknown Decks and out-of-bounds coordinates are refused without touching
// the document.
void invalidTargetsAreRefused() {
    EditorSession session;

    requireThrows([&session] { session.toggleCell(1, 0, 0); },
                 "toggling a Level the station does not have");
    requireThrows([&session] { session.toggleCell(0, kDefaultLevelWidth, 0); },
                 "toggling a column outside the Level");
    requireThrows([&session] { session.toggleCell(0, 0, kDefaultLevelDepth); },
                 "toggling a row outside the Level");
    requireThrows([&session] { session.beginStroke(0, 0, kDefaultLevelDepth); },
                 "starting a stroke outside the Level");
    requireThrows([&session] { session.station().level(1); },
                 "reading a Level the station does not have");

    require(session.station() == Installation::createDefault(),
            "refused edits leave the document untouched");
    require(!session.canUndo(),
            "a refused stroke start leaves no history entry");
}

}  // namespace

int main() {
    try {
        launchCreatesTheDefaultStation();
        newCreatesTheDefaultStation();
        togglingFlipsBothDirections();
        strokeModeFollowsTheStartingCell();
        strokeRevisitsDoNotToggle();
        oneGestureIsOneUndoStep();
        clicksAndGesturesStepThroughHistoryInOrder();
        newEditInvalidatesRedoBranch();
        newDocumentClearsHistory();
        replaceDocumentEstablishesAFreshBaseline();
        emptyGestureLeavesNoHistoryEntry();
        historylessUndoAndRedoAreSafe();
        paintingOffTheDeckIsIgnored();
        invalidTargetsAreRefused();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Editor session consumer passed " << g_checks << " checks\n";
    return 0;
}
