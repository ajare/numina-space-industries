#include "EditorFonts.h"
#include "imgui.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    ImGui::CreateContext();
    try {
        auto& io = ImGui::GetIO();
        const std::filesystem::path resources(argv[1]);
        bool missing_rejected = false;
        try {
            loadEditorFonts(io, resources / "missing-font-directory");
        } catch (const std::runtime_error& error) {
            missing_rejected = std::string(error.what()).find("fa-solid-900.ttf") != std::string::npos;
        }
        if (!missing_rejected) throw std::runtime_error("Missing font diagnostic not reported");

        loadEditorFonts(io, resources);
        if (io.Fonts->ConfigData.Size != 3 || io.Fonts->Fonts.Size != 1) {
            throw std::runtime_error("Expected default, solid and regular merged into one font");
        }
        if (!io.Fonts->Build()) throw std::runtime_error("Font atlas build failed");
        // Actual glyph lookup (not fallback) proves the toolbar/menu icons exist.
        for (ImWchar glyph : {ImWchar('A'), ImWchar(0xf0e2), ImWchar(0xf01e),
                             ImWchar(0xf15b), ImWchar(0xf07c), ImWchar(0xf0c7),
                             ImWchar(0xf0c5), ImWchar(0xf2f5)}) {
            if (io.Fonts->Fonts[0]->FindGlyphNoFallback(glyph) == nullptr) {
                throw std::runtime_error("Missing text or Font Awesome glyph");
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        ImGui::DestroyContext();
        return 1;
    }
    ImGui::DestroyContext();
    return 0;
}
