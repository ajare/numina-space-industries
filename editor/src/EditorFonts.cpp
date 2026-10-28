#include "EditorFonts.h"

#include "imgui.h"
#include "imgui/IconsFontAwesome5.h"

#include <stdexcept>

void loadEditorFonts(ImGuiIO& io, const std::filesystem::path& resource_directory) {
    // Check before ImGui's loader, which asserts on missing files in some builds.
    for (const char* filename : {FONT_ICON_FILE_NAME_FAS, FONT_ICON_FILE_NAME_FAR}) {
        const auto path = resource_directory / filename;
        if (!std::filesystem::is_regular_file(path)) {
            throw std::runtime_error("Missing Editor font: " + path.string());
        }
    }

    io.Fonts->AddFontDefault();
    constexpr float icon_size = 13.0f * 2.0f / 3.0f;
    // ImGui retains the ranges until the atlas is built.
    static const ImWchar ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;
    config.GlyphMinAdvanceX = icon_size;
    // Solid wins for shared codepoints; regular supplies any remaining glyphs.
    for (const char* filename : {FONT_ICON_FILE_NAME_FAS, FONT_ICON_FILE_NAME_FAR}) {
        const auto path = (resource_directory / filename).string();
        if (io.Fonts->AddFontFromFileTTF(path.c_str(), icon_size, &config, ranges) == nullptr) {
            throw std::runtime_error("Could not load Editor font: " + path);
        }
    }
}
