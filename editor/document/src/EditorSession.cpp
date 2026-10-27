#include "station/editor/EditorSession.h"

#include <stdexcept>
#include <utility>
namespace station::editor {

EditorSession::EditorSession() : mStation(SpaceStation::createDefault()) {}

void EditorSession::newDocument() {
    replaceDocument(SpaceStation::createDefault());
}

void EditorSession::replaceDocument(SpaceStation station) {
    // A different document starts clean: no stroke in flight, and no
    // history that could undo the previous document's Cells into it.
    mStroke.reset();
    mStrokeState.reset();
    clearHistory();
    mStation = std::move(station);
}

void EditorSession::toggleCell(std::size_t deck_index, std::uint32_t x,
                              std::uint32_t y) {
    beginStroke(deck_index, x, y);
    paintStroke(x, y);
    endStroke();
}

void EditorSession::beginStroke(std::size_t deck_index, std::uint32_t x,
                               std::uint32_t y) {
    // Sampled before anything is painted, so the stroke's state is the
    // starting Cell's original state inverted.
    const CellState start = mStation.deck(deck_index).cell(x, y);
    mStrokeDeck = deck_index;
    mStrokeState = start == CellState::Filled ? CellState::Void
                                            : CellState::Filled;
    mStroke = Gesture{};
}

void EditorSession::paintStroke(std::uint32_t x, std::uint32_t y) {
    if (!strokeActive() || !mStrokeState.has_value()) {
        return;
    }
    if (!mStation.deck(mStrokeDeck).contains(x, y)) {
        return;
    }
    record(mStrokeDeck, x, y, *mStrokeState);
}

void EditorSession::endStroke() {
    if (!strokeActive()) {
        return;
    }
    Gesture gesture = std::move(*mStroke);
    mStroke.reset();
    mStrokeState.reset();
    if (!gesture.empty()) {
        commitGesture(std::move(gesture));
    }
}

void EditorSession::undo() {
    // A gesture still held down is committed before stepping back, so the
    // history never holds a half-finished stroke.
    endStroke();
    if (mUndoHistory.empty()) {
        return;
    }
    Gesture gesture = std::move(mUndoHistory.back());
    mUndoHistory.pop_back();
    applyGesture(gesture, false);
    mRedoHistory.push_back(std::move(gesture));
}

void EditorSession::redo() {
    endStroke();
    if (mRedoHistory.empty()) {
        return;
    }
    Gesture gesture = std::move(mRedoHistory.back());
    mRedoHistory.pop_back();
    applyGesture(gesture, true);
    mUndoHistory.push_back(std::move(gesture));
}

void EditorSession::record(std::size_t deck_index, std::uint32_t x,
                          std::uint32_t y, CellState next) {
    if (!mStroke) {
        return;
    }
    const CellState previous = mStation.deck(deck_index).cell(x, y);
    if (previous == next) {
        return;
    }
    mStation.setCell(deck_index, x, y, next);
    mStroke->push_back(Change{deck_index, x, y, previous, next});
}

void EditorSession::commitGesture(Gesture gesture) {
    mUndoHistory.push_back(std::move(gesture));
    // A fresh edit abandons the redo branch: undone work cannot be
    // restored alongside a different choice.
    mRedoHistory.clear();
}

void EditorSession::applyGesture(Gesture& gesture, bool forward) {
    if (forward) {
        for (const Change& change : gesture) {
            mStation.setCell(change.deckIndex, change.x, change.y, change.next);
        }
        return;
    }
    for (auto change = gesture.rbegin(); change != gesture.rend(); ++change) {
        mStation.setCell(change->deckIndex, change->x, change->y,
                        change->previous);
    }
}

void EditorSession::clearHistory() {
    mUndoHistory.clear();
    mRedoHistory.clear();
}

}  // namespace station::editor
