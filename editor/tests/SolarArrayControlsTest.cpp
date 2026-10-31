#include "SolarArrayControls.h"
#include "station/editor/EditorSession.h"
#include "imgui.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace station::editor;
namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void check() {
    EditorSession session;
    require(session.installation().solarArrayEnabled(), "new document solar is not enabled");
    const auto original = session.installation().serialize();
    require(original.find("solarArrayEnabled") == std::string::npos, "default adds wire churn");
    Installation legacy;
    legacy.setSolarArrayEnabled(false);
    legacy.deserialize(original);
    require(legacy.solarArrayEnabled(), "missing field did not restore enabled default");

    auto& io = ImGui::GetIO();
    io.DisplaySize = {640, 480}; io.DeltaTime = 1.0f/60;
    io.IniFilename = nullptr;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImVec2 point;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({400,200});
        ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoTitleBar);
        drawSolarArrayControls(session);
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        point = {(min.x+max.x)/2, (min.y+max.y)/2};
        ImGui::End(); ImGui::Render();
    };
    auto click = [&] {
        io.AddMousePosEvent(point.x, point.y); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMouseButtonEvent(0,false); frame();
    };
    frame(); frame(); click();
    require(!session.installation().solarArrayEnabled(), "checkbox did not write authored flag");
    require(session.unsavedChanges(), "checkbox did not dirty document");
    session.undo();
    require(session.installation().serialize()==original && !session.unsavedChanges(), "undo did not restore document");
    session.redo();
    require(!session.installation().solarArrayEnabled(), "redo did not restore disabled choice");

    // Exercise the actual Editor file boundary, not just a settings copy.
    YamlDocumentFiles files;
    const auto path = std::filesystem::temp_directory_path()/"nsi-solar-controls-test.yaml";
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove(p); } } cleanup{path};
    files.write(path,session.installation());
    auto loaded=files.read(path);
    require(loaded==session.installation(), "disabled design changed during save/reload");
    session.replaceDocument(std::move(loaded));
    require(!session.unsavedChanges(), "loaded design is dirty");
    session.setEditingEnabled(false);
    click();
    require(!session.installation().solarArrayEnabled() && !session.unsavedChanges(), "locked checkbox mutated document");
    session.setSolarArrayEnabled(true);
    require(!session.installation().solarArrayEnabled(), "session command bypassed playback lock");
    session.setEditingEnabled(true); click();
    require(session.installation().solarArrayEnabled() && session.unsavedChanges(), "reset did not unlock checkbox");
    files.write(path,session.installation());
    require(files.read(path)==session.installation(), "enabled design changed during save/reload");
}
}
int main() {
    ImGui::CreateContext();
    try { check(); }
    catch (const std::exception& e) { ImGui::DestroyContext(); std::cerr<<e.what()<<'\n'; return 1; }
    ImGui::DestroyContext();
}
