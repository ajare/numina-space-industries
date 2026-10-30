#pragma once
#include "InstallationRenderMode.h"

namespace station::editor {
void drawInstallationRenderModeCombo(InstallationRenderSettings&);
// Draws nothing in Default mode. View controls stay enabled during playback.
void drawThermalRenderControls(InstallationRenderSettings&);
void drawPressureRenderControls(InstallationRenderSettings&);
void drawAtmosphereRenderControls(InstallationRenderSettings&);
// Independent of render mode; hidden when the hull border is turned off.
void drawHullRenderControls(InstallationRenderSettings&);
} // namespace station::editor
