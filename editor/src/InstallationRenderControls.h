#pragma once
#include "InstallationRenderMode.h"

namespace station::editor {
void drawInstallationRenderModeCombo(InstallationRenderSettings&);
// Draws nothing in Default mode. View controls stay enabled during playback.
void drawThermalRenderControls(InstallationRenderSettings&);
} // namespace station::editor
