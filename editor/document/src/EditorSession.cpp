#include "station/editor/EditorSession.h"

#include <stdexcept>
#include <utility>
namespace station::editor {

EditorSession::EditorSession()
    : mStation(SpaceStation::createDefault()), mBaseline(mStation) {}

FileResult EditorSession::request(DocumentAction action, UnsavedChangesPrompt& prompt,
                                  FileDialogs& dialogs, DocumentFiles& files) {
    // A window close can interrupt a held pointer gesture. Keep that whole
    // edit undoable if the user cancels, and save exactly the visible geometry.
    endStroke();
    try {
        if (unsavedChanges()) {
            switch (prompt.confirm(action, mPath)) {
                case UnsavedDecision::Cancel:
                    return {FileOutcome::Cancelled, {}};
                case UnsavedDecision::Save: {
                    auto result = save(dialogs, files);
                    if (result.outcome != FileOutcome::Success) return result;
                    break;
                }
                case UnsavedDecision::Discard:
                    break;
            }
        }
        switch (action) {
            case DocumentAction::New:
                newDocument();
                return {FileOutcome::Success, {}};
            case DocumentAction::Open:
                return open(dialogs, files);
            case DocumentAction::Exit:
                return {FileOutcome::Success, {}};
        }
        return {FileOutcome::Failed, "Unknown document action"};
    } catch (const std::exception& error) {
        return {FileOutcome::Failed, std::string("Document action failed: ") + error.what()};
    }
}

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
    mBaseline = mStation;
    mPath.clear();
}

FileResult EditorSession::open(FileDialogs& dialogs, DocumentFiles& files) {
    try {
        const auto selection = dialogs.openYaml();
        if (selection.outcome != FileOutcome::Success) {
            return {selection.outcome, selection.diagnostic};
        }
        if (selection.path.empty()) return {FileOutcome::Failed, "Open returned an empty path"};
        auto loaded = files.read(selection.path);
        replaceDocument(std::move(loaded));
        mPath = selection.path;
        return {FileOutcome::Success, {}};
    } catch (const std::exception& error) {
        return {FileOutcome::Failed, std::string("Open failed: ") + error.what()};
    }
}

FileResult EditorSession::save(FileDialogs& dialogs, DocumentFiles& files) {
    if (mPath.empty()) return saveAs(dialogs, files);
    return saveTo({FileOutcome::Success, mPath, {}}, files);
}

FileResult EditorSession::saveAs(FileDialogs& dialogs, DocumentFiles& files) {
    try {
        return saveTo(dialogs.saveYaml(mPath), files);
    } catch (const std::exception& error) {
        return {FileOutcome::Failed, std::string("Save failed: ") + error.what()};
    }
}

FileResult EditorSession::saveTo(const FileSelection& selection, DocumentFiles& files) {
    if (selection.outcome != FileOutcome::Success) {
        return {selection.outcome, selection.diagnostic};
    }
    try {
        if (selection.path.empty()) throw std::runtime_error("Save returned an empty path");
        files.write(selection.path, mStation);
        mPath = selection.path;
        mBaseline = mStation;
        return {FileOutcome::Success, {}};
    } catch (const std::exception& error) {
        return {FileOutcome::Failed, std::string("Save failed: ") + error.what()};
    }
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
