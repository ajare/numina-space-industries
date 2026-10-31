#pragma once
#include <station/StationSystems.h>
#include <optional>
namespace station::editor {
class EditorSession;
// Returns true only when the authored choice changes.
bool drawSolarArrayControls(EditorSession& session);
bool drawSolarArrayPanel(EditorSession&, const std::optional<StationSystemsSnapshot>&);
}
