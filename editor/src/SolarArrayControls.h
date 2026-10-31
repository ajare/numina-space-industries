#pragma once
namespace station::editor {
class EditorSession;
// Returns true only when the authored choice changes.
bool drawSolarArrayControls(EditorSession& session);
}
