#pragma once

#include <filesystem>

struct ImGuiIO;

// Merge Font Awesome into the default text font before the first frame.
// Throws with a diagnostic if the runtime font assets cannot be loaded.
void loadEditorFonts(ImGuiIO& io, const std::filesystem::path& resource_directory);
