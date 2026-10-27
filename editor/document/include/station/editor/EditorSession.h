// The headless Editor session: the one seam shared by the Editor GUI and the
// automated behaviour tests.
//
// It owns the active document, the gesture in progress, and the undo/redo
// history, so tests can drive the same path the GUI takes without a
// desktop, a GL context, or a native file dialog. Graphics, dialogs, and
// file I/O stay outside this boundary and are supplied by adapters.
//
// Editing is gesture-shaped: a click is a one-Cell stroke and a drag is a
// many-Cell stroke, and each completed stroke is exactly one undo step no
// matter how many Cells it touched.
#pragma once

#include "station/editor/CellState.h"
#include "station/editor/SpaceStation.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace station::editor {

class EditorSession {
public:
    // Launch state: an untitled document holding the default Space Station,
    // with no history.
    EditorSession();

    // File > New: replace the document with a fresh default Space Station
    // and drop its history.
    void newDocument();

    // Establish another document as the active one, with no history: the
    // previous document's edits can never be undone into it, and the new
    // document's own past is not carried over. This is what a successful
    // Open uses.
    void replaceDocument(SpaceStation station);

    const SpaceStation& station() const noexcept { return mStation; }

    // Toggle one Cell as a complete one-Cell gesture: one undo step. Throw
    // std::out_of_range for an unknown Deck or coordinate.
    void toggleCell(std::size_t deck_index, std::uint32_t x, std::uint32_t y);

    // --- Drag strokes ------------------------------------------------------
    // beginStroke fixes the stroke's paint state as the opposite of the
    // starting Cell's state at that moment: starting on Filled paints Void,
    // starting on Void paints Filled. Every Cell the stroke visits — the
    // starting Cell included, painted by the caller with paintStroke — is
    // set to that state rather than toggled, so revisiting a Cell within the
    // same gesture cannot undo an earlier pass. Throw std::out_of_range for
    // an unknown Deck or starting coordinate.
    void beginStroke(std::size_t deck_index, std::uint32_t x, std::uint32_t y);
    bool strokeActive() const noexcept { return mStroke.has_value(); }

    // Paint one Cell visited by the active stroke. Outside a stroke, or on a
    // coordinate the stroked Deck does not contain, this is ignored: a
    // pointer can wander off the Deck mid-drag and that is not an error.
    void paintStroke(std::uint32_t x, std::uint32_t y);

    // Finish the gesture. Commits it as one undo step when it changed at
    // least one Cell; a stroke that changed nothing leaves no history entry,
    // so undo never has to step over an empty gesture.
    void endStroke();

    // --- History ---------------------------------------------------------
    // Undo and redo one whole gesture. A new edit after an undo discards the
    // abandoned redo branch. With nothing to undo or redo these do nothing.
    void undo();
    void redo();
    bool canUndo() const noexcept { return !mUndoHistory.empty(); }
    bool canRedo() const noexcept { return !mRedoHistory.empty(); }

private:
    // One Cell touched by a gesture, with both sides of the change so the
    // gesture can be reversed and re-applied exactly.
    struct Change {
        std::size_t deckIndex;
        std::uint32_t x;
        std::uint32_t y;
        CellState previous;
        CellState next;
    };
    using Gesture = std::vector<Change>;

    // Apply one Cell change if it differs from the current state.
    void record(std::size_t deck_index, std::uint32_t x, std::uint32_t y,
               CellState next);
    // Push a completed gesture onto the undo stack, dropping the redo branch.
    void commitGesture(Gesture gesture);
    void applyGesture(Gesture& gesture, bool forward);
    void clearHistory();

    SpaceStation mStation;
    std::size_t mStrokeDeck = 0;
    std::optional<CellState> mStrokeState;
    std::optional<Gesture> mStroke;
    std::vector<Gesture> mUndoHistory;
    std::vector<Gesture> mRedoHistory;
};

}  // namespace station::editor
