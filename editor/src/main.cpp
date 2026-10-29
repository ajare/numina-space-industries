// Editor application shell.
//
// A full-viewport host frame owns the document File menu, toolbar,
// and statusbar; a DockSpace between them holds the
// "Editor" inspector docked to the left and the Level Grid in the central
// node. All editing behaviour
// lives in the headless station::editor::EditorSession; this file only
// draws, forwards input, and supplies native-dialog and storage adapters.

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder* (initial dock layout) is internal API
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"

#include "imgui/IconsFontAwesome5.h"
#include "EditorFonts.h"
#include "EditorEvents.h"
#include "DeckGridWidget.h"
#include "NativeFileDialogs.h"
#include "SimulationPlayback.h"

#include "station/editor/EditorSession.h"
#include "station/editor/Installation.h"

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <cstring>

namespace {

constexpr int kInitialWindowWidth = 1280;
constexpr int kInitialWindowHeight = 800;
constexpr float kInitialLeftDockWidthPx = 500.0f;
constexpr const char* kPanelTitle = "Editor";
constexpr const char* kGridTitle = "Level Grid";

class SdlUnsavedChangesPrompt final : public station::editor::UnsavedChangesPrompt {
public:
    explicit SdlUnsavedChangesPrompt(SDL_Window* window) : mWindow(window) {}

    station::editor::UnsavedDecision confirm(station::editor::DocumentAction action,
                                            const std::filesystem::path& path) override {
        using station::editor::DocumentAction;
        using station::editor::UnsavedDecision;
        const char* next = action == DocumentAction::New ? "creating a new Installation" :
                           action == DocumentAction::Open ? "opening another Installation" :
                           "exiting the Editor";
        const std::string message = "Save changes to " +
            (path.empty() ? std::string("Untitled") : path.filename().string()) +
            " before " + next + "?";
        const SDL_MessageBoxButtonData buttons[] = {
            {0, 1, "Save"},
            {0, 2, "Discard"},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,
             0, "Cancel"}
        };
        const SDL_MessageBoxData data = {SDL_MESSAGEBOX_WARNING, mWindow,
            "Unsaved Installation changes", message.c_str(), 3, buttons, nullptr};
        int chosen = 0;
        if (SDL_ShowMessageBox(&data, &chosen) != 0) {
            throw std::runtime_error(std::string("Unsaved prompt failed: ") + SDL_GetError());
        }
        if (chosen == 1) return UnsavedDecision::Save;
        if (chosen == 2) return UnsavedDecision::Discard;
        return UnsavedDecision::Cancel;
    }

private:
    SDL_Window* mWindow;
};

// ImGui loads imgui.ini during the first NewFrame(), so the presence of a saved
// layout must be sampled before then. When one exists it wins over the built-in
// initial layout; the DockBuilder would otherwise stomp the user's arrangement.
bool hasSavedLayout(const ImGuiIO& io) {
    return io.IniFilename != nullptr && std::filesystem::exists(io.IniFilename);
}

void buildInitialDockLayout(ImGuiID dockspace_id, const ImVec2& size) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    // The node must carry the real dockspace size before splitting, or the
    // ratio resolves against the builder's tiny default node.
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    const float safe_width = std::max(size.x, 1.0f);
    const float ratio =
        std::clamp(kInitialLeftDockWidthPx / safe_width, 0.1f, 0.9f);
    ImGuiID left = 0;
    ImGuiID right = 0;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, ratio, &left, &right);

    // The left panel renders as a plain window: its dock node never shows a
    // tab bar, so the content fills the node directly.
    if (ImGuiDockNode* left_node = ImGui::DockBuilderGetNode(left)) {
        left_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    }

    ImGui::DockBuilderDockWindow(kPanelTitle, left);
    ImGui::DockBuilderDockWindow(kGridTitle, right);
    ImGui::DockBuilderFinish(dockspace_id);
}

}  // namespace

int runEditor() {
    NativeFileDialogs dialogs;
    station::editor::YamlDocumentFiles files;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow(
        "Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kInitialWindowWidth, kInitialWindowHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);  // vsync; failure to enable is not fatal

    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Layout persistence stays on the default: io.IniFilename == "imgui.ini".
    const bool saved_layout_exists = hasSavedLayout(io);

    char* base_path = SDL_GetBasePath();
    try {
        if (base_path == nullptr) {
            throw std::runtime_error(std::string("SDL_GetBasePath failed: ") + SDL_GetError());
        }
        loadEditorFonts(io, std::filesystem::path(base_path) / "resources");
    } catch (const std::exception& error) {
        SDL_free(base_path);
        SDL_Log("Editor font initialisation failed: %s", error.what());
        ImGui::DestroyContext(context);
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_free(base_path);

    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        SDL_Log("ImGui_ImplOpenGL3_Init failed");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        ImGui::DestroyContext(context);
        SDL_Quit();
        return 1;
    }
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);

    bool running = true;
    bool first_frame = true;
    // The one headless editing session: the GUI holds no geometry of its
    // own, it only renders and drives what the session reports.
    station::editor::EditorSession session;
    station::editor::SimulationPlayback simulation;
    SdlUnsavedChangesPrompt unsaved_prompt(window);
    std::string file_status;
    auto report = [&](const station::editor::FileResult& result) {
        using station::editor::FileOutcome;
        if (result.outcome == FileOutcome::Failed) {
            file_status = result.diagnostic;
            SDL_Log("%s", file_status.c_str());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Installation file error",
                                     file_status.c_str(), window);
        } else if (result.outcome == FileOutcome::Success) {
            file_status = "File operation completed";
        }
    };

    int new_size[2] = {16,16};
    int added_size[2] = {16,16};
    int added_offset[2] = {0,0};
    bool request_new = false;
    int mode_index = 0;
    std::size_t active_level = 0;
    std::optional<DeckCellCoord> selected_cell;
    auto edit = [&](auto operation) {
        try { file_status.clear(); operation(); }
        catch (const std::exception& error) { file_status=error.what(); }
    };
    auto perform = [&](station::editor::DocumentAction action) {
        const auto result = session.request(action, unsaved_prompt, dialogs, files,
            static_cast<std::uint32_t>(new_size[0]),static_cast<std::uint32_t>(new_size[1]));
        if (result.outcome == station::editor::FileOutcome::Success) {
            file_status.clear();
            if (action == station::editor::DocumentAction::Exit) running = false;
            else if (action == station::editor::DocumentAction::New ||
                     action == station::editor::DocumentAction::Open) {
                // Even an identical replacement document starts a fresh preview.
                try { simulation.reset(session.installation()); }
                catch (const std::exception& error) { file_status = error.what(); }
            }
        } else {
            report(result);
        }
        return result;
    };

    while (running) {
        bool close_requested = false;
        processEditorEvents([&](SDL_Event& event) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT &&
                 event.window.windowID == SDL_GetWindowID(window) &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE)) {
                close_requested = true;
            }
        });
        // Coalesce duplicate close events; Cancel keeps the application alive.
        if (close_requested) perform(station::editor::DocumentAction::Exit);
        if (!running) {
            break;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Undo/redo hotkeys, handled here rather than as menu shortcuts so
        // one press fires exactly one step whether or not the Edit menu is
        // open (a MenuItem shortcut would fire again from the menu).
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            session.undo();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            session.redo();
        }

        // --- Host frame: menubar + toolbar + dockspace + statusbar ----------
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        const ImGuiWindowFlags host_flags =
            ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoNavFocus;

        ImGui::Begin("EditorHost", nullptr, host_flags);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem(ICON_FA_FILE " New")) {
                    request_new = true;
                }
                if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open...")) {
                    perform(station::editor::DocumentAction::Open);
                }
                if (ImGui::MenuItem(ICON_FA_SAVE " Save")) {
                    report(session.save(dialogs, files));
                }
                if (ImGui::MenuItem(ICON_FA_COPY " Save As...")) {
                    report(session.saveAs(dialogs, files));
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_SIGN_OUT_ALT " Exit")) {
                    perform(station::editor::DocumentAction::Exit);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                // The chord is spelled out in the label: passing it as the
                // shortcut would register a second handler for it.
                if (ImGui::MenuItem(ICON_FA_UNDO " Undo  (Ctrl+Z)", nullptr, false,
                                   session.canUndo())) {
                    session.undo();
                }
                if (ImGui::MenuItem(ICON_FA_REDO " Redo  (Ctrl+Y)", nullptr, false,
                                   session.canRedo())) {
                    session.redo();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (request_new) { ImGui::OpenPopup("New Installation"); request_new=false; }
        if (ImGui::BeginPopupModal("New Installation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputInt2("Width / depth",new_size);
            ImGui::BeginDisabled(new_size[0]<=0 || new_size[1]<=0);
            if (ImGui::Button("Create")) {
                const auto result=perform(station::editor::DocumentAction::New);
                if (result.outcome==station::editor::FileOutcome::Success) {
                    active_level=0; selected_cell.reset();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Toolbar: a fixed-height framed strip. The undo/redo buttons drive
        // the same session calls as the hotkeys and menu items, and are
        // disabled exactly when the session has nothing to step over.
        const ImVec2 toolbar_min = ImGui::GetCursorScreenPos();
        const float toolbar_width = ImGui::GetContentRegionAvail().x;
        const float toolbar_height = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddRect(
            toolbar_min,
            ImVec2(toolbar_min.x + toolbar_width, toolbar_min.y + toolbar_height),
            ImGui::GetColorU32(ImGuiCol_Border));
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, !session.canUndo());
        if (ImGui::Button(ICON_FA_UNDO " Undo")) {
            session.undo();
        }
        ImGui::PopItemFlag();
        ImGui::SameLine();
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, !session.canRedo());
        if (ImGui::Button(ICON_FA_REDO " Redo")) {
            session.redo();
        }
        ImGui::PopItemFlag();

        try { simulation.synchronize(session.hasDocument() ? &session.installation() : nullptr); }
        catch (const std::exception& error) { file_status = error.what(); }
        ImGui::SameLine();
        ImGui::BeginDisabled(simulation.runtime() == nullptr);
        if (ImGui::Button(simulation.playing() ? ICON_FA_PAUSE " Pause" : ICON_FA_PLAY " Play"))
            simulation.togglePlaying();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!session.hasDocument());
        if (ImGui::Button(ICON_FA_UNDO " Reset"))
            edit([&]{ simulation.reset(session.installation()); });
        ImGui::SameLine();
        ImGui::SetNextItemWidth(85);
        int speed_index = simulation.speedIndex();
        if (ImGui::Combo("Speed", &speed_index, "0.1x\0" "0.5x\0" "1x\0" "2x\0" "10x\0"))
            simulation.setSpeedIndex(speed_index);
        ImGui::EndDisabled();
        // Do not erase an earlier diagnostic on every successful frame.
        try { simulation.update(io.DeltaTime); }
        catch (const std::exception& error) { file_status = error.what(); }
        ImGui::SameLine();
        ImGui::Text("%.1f s", simulation.runtime() ? simulation.runtime()->snapshot().elapsedSeconds : 0.0);

        // Plain-letter shortcuts must not consume text-field input or chords.
        if (!io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper && !io.KeyShift &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            int requested_mode = mode_index;
            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) requested_mode = static_cast<int>(EditMode::Cell);
            else if (ImGui::IsKeyPressed(ImGuiKey_W, false)) requested_mode = static_cast<int>(EditMode::Wall);
            else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) requested_mode = static_cast<int>(EditMode::Sector);
            if (requested_mode != mode_index) {
                session.endStroke();
                mode_index = requested_mode;
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        if (ImGui::Combo("Mode", &mode_index, "Cell (C)\0Wall (W)\0Sector (S)\0")) session.endStroke();
        const auto mode=static_cast<EditMode>(mode_index);
        active_level=std::min(active_level,session.installation().levelCount()-1);
        if (ImGui::FindWindowSettingsByID(ImHashStr(kGridTitle)) == nullptr) {
            // Add the grid to existing user layouts without rebuilding them.
            const ImGuiID saved_dockspace=ImGui::GetID("EditorDockSpace");
            if (auto* central=ImGui::DockBuilderGetCentralNode(saved_dockspace))
                ImGui::DockBuilderDockWindow(kGridTitle,central->ID);
        }

        // Reserve the statusbar's height so the dockspace stops above it.
        const float statusbar_reserve =
            ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;

        const ImGuiID dockspace_id = ImGui::GetID("EditorDockSpace");
        if (first_frame && !saved_layout_exists) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            buildInitialDockLayout(
                dockspace_id,
                ImVec2(avail.x, avail.y - statusbar_reserve));
        }
        first_frame = false;
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, -statusbar_reserve),
                        ImGuiDockNodeFlags_None);

        ImGui::Separator();
        ImGui::Text("%s - %zu of %zu Cells Interior",
                   session.unsavedChanges() ? "Unsaved changes" : "Ready",
                   session.installation().level(active_level).interiorCount(),
                   session.installation().level(active_level).cellCount());
        ImGui::SameLine();
        ImGui::Text("| Sim update: %.1f \xC2\xB5s (1s avg)",
                    simulation.runtime() ? simulation.runtime()->averageStepMicroseconds() : 0.0);
        if (!file_status.empty()) {
            ImGui::SameLine();
            ImGui::TextUnformatted(file_status.c_str());
        }
        ImGui::End();
        const std::string title = "Editor - " +
            (session.path().empty() ? std::string("Untitled") : session.path().string()) +
            (session.unsavedChanges() ? " *" : "");
        SDL_SetWindowTitle(window, title.c_str());

        // --- Installation and Sector inspector (left) ---------------------
        ImGui::Begin(kPanelTitle);
        if (ImGuiWindow* panel = ImGui::GetCurrentWindow(); panel->DockNode)
            panel->DockNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        auto textField = [&](const char* label, const std::string& value, auto commit) {
            std::vector<char> buffer(value.size()+1024,0);
            std::copy(value.begin(),value.end(),buffer.begin());
            if (ImGui::InputText(label,buffer.data(),buffer.size()))
                edit([&]{ commit(std::string(buffer.data())); });
        };
        textField("Installation name",session.installation().getName(),
                  [&](std::string name){ session.setName(std::move(name)); });
        using HullType = nsi::core::Installation::HullType;
        const auto hull = session.installation().hullType();
        int hullIndex = hull ? static_cast<int>(*hull) + 1 : 0;
        const char* hullNames[] = {"No hull", "Bare skin", "Standard insulated", "Multi-layer blanket"};
        if (ImGui::Combo("Hull type", &hullIndex, hullNames, 4))
            edit([&]{ session.setHullType(hullIndex == 0 ? std::nullopt
                : std::optional(static_cast<HullType>(hullIndex - 1))); });
        using PartitionType = nsi::core::Installation::PartitionType;
        const auto partition = session.installation().partitionType();
        int partitionIndex = partition ? static_cast<int>(*partition) + 1 : 0;
        const char* partitionNames[] = {"No partition coupling", "Bare partition", "Thermal break", "Insulated partition"};
        if (ImGui::Combo("Partition type", &partitionIndex, partitionNames, 4))
            edit([&]{ session.setPartitionType(partitionIndex == 0 ? std::nullopt
                : std::optional(static_cast<PartitionType>(partitionIndex - 1))); });
        const auto level_label="Level "+std::to_string(active_level);
        if (ImGui::BeginCombo("Level",level_label.c_str())) {
            for (std::size_t l=0;l<session.installation().levelCount();++l) {
                const auto label="Level "+std::to_string(l);
                if (ImGui::Selectable(label.c_str(),l==active_level)) {
                    session.endStroke(); active_level=l; selected_cell.reset();
                }
            }
            ImGui::EndCombo();
        }
        const auto inspected=session.installation().level(active_level);
        ImGui::Text("Size: %u x %u",inspected.width(),inspected.depth());
        int offsets[2]={inspected.offsetX(),inspected.offsetY()};
        if (ImGui::InputInt2("Offset",offsets))
            edit([&]{ session.setOffset(active_level,offsets[0],offsets[1]); });
        if (ImGui::CollapsingHeader("Add Level")) {
            ImGui::InputInt2("New width / depth",added_size);
            ImGui::InputInt2("New offset",added_offset);
            if (ImGui::Button("Append Void Level")) edit([&]{
                if (added_size[0]<=0 || added_size[1]<=0)
                    throw std::invalid_argument("Level dimensions must be positive");
                session.addLevel(added_size[0],added_size[1],added_offset[0],added_offset[1]);
                active_level=session.installation().levelCount()-1; selected_cell.reset();
            });
        }
        ImGui::BeginDisabled(session.installation().levelCount()==1);
        if (ImGui::Button("Remove top Level")) {
            edit([&]{
                if (!session.removeTopLevel())
                    file_status="Top Level cleared: a lower Sector still claims its volume";
                active_level=std::min(active_level,session.installation().levelCount()-1);
                selected_cell.reset();
            });
        }
        ImGui::EndDisabled();
        if (mode==EditMode::Sector && selected_cell) {
            const auto current=session.installation().level(active_level);
            if (current.contains(selected_cell->x,selected_cell->y)) {
                const int id=current.cellData(selected_cell->x,selected_cell->y).sector;
                const auto sector=current.sectors()[id];
                if (ImGui::CollapsingHeader("Selected Sector",ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Text("Sector %d | Level %d | Area %zu",id,sector.level,sector.area);
                    ImGui::Text("Type: %s",sector.type==nsi::core::Installation::SectorType::Interior ? "Interior" : "Void");
                    textField("Name",sector.name,[&](std::string name){
                        session.setSector(active_level,id,std::move(name),sector.height);
                    });
                    int height=sector.height;
                    ImGui::BeginDisabled(sector.type==nsi::core::Installation::SectorType::Void);
                    if (ImGui::InputInt("Height",&height)) edit([&]{
                        session.setSector(active_level,id,session.installation().level(active_level).sectors()[id].name,height);
                    });
                    ImGui::EndDisabled();
                }
            }
        }
        ImGui::End();

        // --- Grid input ----------------------------------------------------
        ImGui::Begin(kGridTitle);
        const auto grid=session.installation().level(active_level);
        std::optional<int> selected_sector;
        if (mode==EditMode::Sector && selected_cell && grid.contains(selected_cell->x,selected_cell->y))
            selected_sector=grid.cellData(selected_cell->x,selected_cell->y).sector;
        const auto pointer=drawDeckGrid("LevelGrid",grid,mode,selected_sector);
        const bool stroke_event=session.strokeActive() &&
            (pointer.phase==DeckPointerPhase::Release ||
             (pointer.phase==DeckPointerPhase::Drag &&
              ((mode==EditMode::Cell && pointer.cell) || (mode==EditMode::Wall && pointer.wall))));
        if (pointer.phase==DeckPointerPhase::Press || stroke_event) edit([&]{
            if (pointer.phase==DeckPointerPhase::Press) {
                if (mode==EditMode::Wall && pointer.wall) {
                    session.beginWallStroke(active_level,*pointer.wall);
                    session.paintWallStroke(*pointer.wall);
                }
                else if (mode==EditMode::Sector) selected_cell=pointer.cell;
                else if (mode==EditMode::Cell && pointer.cell) {
                    session.beginStroke(active_level,pointer.cell->x,pointer.cell->y);
                    session.paintStroke(pointer.cell->x,pointer.cell->y);
                }
            } else if (pointer.phase==DeckPointerPhase::Drag && mode==EditMode::Cell && pointer.cell)
                session.paintStroke(pointer.cell->x,pointer.cell->y);
            else if (pointer.phase==DeckPointerPhase::Drag && mode==EditMode::Wall && pointer.wall)
                session.paintWallStroke(*pointer.wall);
            else if (pointer.phase==DeckPointerPhase::Release) session.endStroke();
        });
        ImGui::End();

        ImGui::Render();
        int drawable_width = 0;
        int drawable_height = 0;
        SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
        glViewport(0, 0, drawable_width, drawable_height);
        glClearColor(0.12f, 0.14f, 0.17f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplSDL2_Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(context);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

int main() {
    try {
        return runEditor();
    } catch (const std::exception& error) {
        SDL_Log("Editor startup failed: %s", error.what());
        return 1;
    }
}
