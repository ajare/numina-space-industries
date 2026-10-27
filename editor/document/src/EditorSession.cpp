#include "station/editor/EditorSession.h"

namespace station::editor {

EditorSession::EditorSession() : mStation(SpaceStation::createDefault()) {}

void EditorSession::newDocument() {
    mStation = SpaceStation::createDefault();
}

void EditorSession::toggleCell(std::size_t deck_index, std::uint32_t x,
                              std::uint32_t y) {
    mStation.toggleCell(deck_index, x, y);
}

}  // namespace station::editor
