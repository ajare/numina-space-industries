// The headless Editor session: the one seam shared by the Editor GUI and the
// automated behaviour tests.
//
// It owns the active document and exposes the user-level commands performed
// on it, so tests can drive the same path the GUI takes without a desktop,
// a GL context, or a native file dialog. Graphics, dialogs, and file I/O
// stay outside this boundary and are supplied by adapters.
#pragma once

#include "station/editor/CellState.h"
#include "station/editor/SpaceStation.h"

#include <cstdint>

namespace station::editor {

class EditorSession {
public:
    // Launch state: an untitled document holding the default Space Station.
    EditorSession();

    // File > New: replace the document with a fresh default Space Station.
    void newDocument();

    const SpaceStation& station() const noexcept { return mStation; }

    // Toggle one Cell of one Deck: Filled becomes Void and Void becomes
    // Filled. Throw std::out_of_range for an unknown Deck or coordinate.
    void toggleCell(std::size_t deck_index, std::uint32_t x, std::uint32_t y);

private:
    SpaceStation mStation;
};

}  // namespace station::editor
