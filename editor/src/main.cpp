// Editor application shell.
//
// A full-viewport host frame owns the document File menu, toolbar,
// and statusbar; a DockSpace between them holds the
// single "Editor" panel docked to the left and the "Overview" tab in the
// central node. The Editor panel edits the active session's Deck geometry
// and the Overview previews that same live geometry. All editing behaviour
// lives in the headless station::editor::EditorSession; this file only
// draws, forwards input, and supplies native-dialog and storage adapters.

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder* (initial dock layout) is internal API
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"

#include "imgui/IconsFontAwesome5.h"
#include "EditorFonts.h"
#include "DeckGridWidget.h"
#include "OverviewRenderer.h"
#include "NativeFileDialogs.h"

#include "station/editor/EditorSession.h"
#include "station/editor/SpaceStation.h"

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace {

constexpr int kInitialWindowWidth = 1280;
constexpr int kInitialWindowHeight = 800;
constexpr float kInitialLeftDockWidthPx = 500.0f;
constexpr const char* kPanelTitle = "Editor";
constexpr const char* kOverviewTitle = "Overview";

class SdlUnsavedChangesPrompt final : public station::editor::UnsavedChangesPrompt {
public:
    explicit SdlUnsavedChangesPrompt(SDL_Window* window) : mWindow(window) {}

    station::editor::UnsavedDecision confirm(station::editor::DocumentAction action,
                                            const std::filesystem::path& path) override {
        using station::editor::DocumentAction;
        using station::editor::UnsavedDecision;
        const char* next = action == DocumentAction::New ? "creating a new Space Station" :
                           action == DocumentAction::Open ? "opening another Space Station" :
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
            "Unsaved Space Station changes", message.c_str(), 3, buttons, nullptr};
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

    // MPP renders the Overview tab offscreen into a render texture that
    // ImGui presents as an image. It must initialise with the GL context
    // current, and shut down before that context dies.
    OverviewRenderer overview;
    {
        int drawable_width = 0;
        int drawable_height = 0;
        SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
        try {
            overview.initialise(static_cast<std::size_t>(drawable_width),
                               static_cast<std::size_t>(drawable_height));
        } catch (const std::exception& error) {
            SDL_Log("MPP initialisation failed: %s", error.what());
            overview.shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext(context);
            SDL_GL_DeleteContext(gl_context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    }

    bool running = true;
    bool first_frame = true;
    // The one headless editing session: the GUI holds no geometry of its
    // own, it only renders and drives what the session reports.
    station::editor::EditorSession session;
    SdlUnsavedChangesPrompt unsaved_prompt(window);
    std::string file_status;
    auto report = [&](const station::editor::FileResult& result) {
        using station::editor::FileOutcome;
        if (result.outcome == FileOutcome::Failed) {
            file_status = result.diagnostic;
            SDL_Log("%s", file_status.c_str());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Space Station file error",
                                     file_status.c_str(), window);
        } else if (result.outcome == FileOutcome::Success) {
            file_status = "File operation completed";
        }
    };

    auto perform = [&](station::editor::DocumentAction action) {
        const auto result = session.request(action, unsaved_prompt, dialogs, files);
        if (result.outcome == station::editor::FileOutcome::Success) {
            file_status.clear();
            if (action == station::editor::DocumentAction::Exit) running = false;
        } else {
            report(result);
        }
    };

    while (running) {
        bool close_requested = false;
        SDL_Event event;
        while (SDL_WaitEventTimeout(&event, 16) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT &&
                 event.window.windowID == SDL_GetWindowID(window) &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE)) {
                close_requested = true;
            }
        }
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
                    perform(station::editor::DocumentAction::New);
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
        // The Overview window claims the right-hand (central) node, which
        // keeps its tab bar: future panels docked alongside join as further
        // tabs. A tree restored from ini only learns its central node during
        // the first docking update pass, so the claim is retried until it
        // lands. Once window settings record the dock, the claim is done and
        // a user-arranged layout survives restarts.
        if (ImGui::FindWindowSettingsByID(ImHashStr(kOverviewTitle)) ==
            nullptr) {
            if (ImGuiDockNode* central =
                    ImGui::DockBuilderGetCentralNode(dockspace_id)) {
                ImGui::DockBuilderDockWindow(kOverviewTitle, central->ID);
            }
        }
        const bool overview_ready =
            ImGui::FindWindowSettingsByID(ImHashStr(kOverviewTitle)) !=
            nullptr;
        first_frame = false;
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, -statusbar_reserve),
                        ImGuiDockNodeFlags_None);

        ImGui::Separator();
        ImGui::Text("%s - %zu of %zu Cells Filled",
                   session.unsavedChanges() ? "Unsaved changes" : "Ready",
                   session.station().deck(0).filledCount(),
                   session.station().deck(0).cellCount());
        if (!file_status.empty()) {
            ImGui::SameLine();
            ImGui::TextUnformatted(file_status.c_str());
        }
        ImGui::End();
        const std::string title = "Editor - " +
            (session.path().empty() ? std::string("Untitled") : session.path().string()) +
            (session.unsavedChanges() ? " *" : "");
        SDL_SetWindowTitle(window, title.c_str());

        // --- The Editor panel ----------------------------------------------
        // The active session's Deck, drawn top-down and clickable. The panel
        // reads geometry from the session and sends clicks back to it; it
        // never keeps a copy of its own.
        const station::editor::Deck& deck = session.station().deck(0);
        // p_open is null, so the panel has no close button and cannot be
        // closed into an empty application.
        ImGui::Begin(kPanelTitle);
        // Layouts restored from an older imgui.ini may still carry a tab bar;
        // re-assert the flag so the panel always renders as a plain window.
        if (ImGuiWindow* panel = ImGui::GetCurrentWindow();
            panel->DockNode != nullptr) {
            panel->DockNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        }
        // Pointer phases drive the session's gestures: a press opens a
        // stroke whose paint state is the opposite of the Cell it started
        // on, dragging paints every Cell it visits, and the release
        // commits the whole gesture as one undo step.
        const DeckPointerEvent pointer = drawDeckGrid("DeckGrid", deck);
        switch (pointer.phase) {
            case DeckPointerPhase::Press:
                if (pointer.cell.has_value()) {
                    // The press paints its own Cell, so a click with no
                    // drag is a one-Cell gesture.
                    session.beginStroke(0, pointer.cell->x, pointer.cell->y);
                    session.paintStroke(pointer.cell->x, pointer.cell->y);
                }
                break;
            case DeckPointerPhase::Drag:
                if (pointer.cell.has_value()) {
                    session.paintStroke(pointer.cell->x, pointer.cell->y);
                }
                break;
            case DeckPointerPhase::Release:
                session.endStroke();
                break;
            case DeckPointerPhase::None:
                break;
        }
        ImGui::End();

        // --- The Overview panel --------------------------------------------
        // Docked right in the tabbed central node: its tab shows now, and
        // more tabs appear as further panels join the node. Created only
        // once the dock claim has landed, so it never flashes floating.
        // p_open is null: the tab carries no close button.
        if (overview_ready) {
            ImGui::Begin(kOverviewTitle);
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const auto grid_width = static_cast<std::size_t>(
                std::max(1.0f, std::floor(available.x)));
            const auto grid_height = static_cast<std::size_t>(
                std::max(1.0f, std::floor(available.y)));
            const std::uint32_t texture_id =
                overview.render(grid_width, grid_height, session.station());
            ImGui::Image(
                reinterpret_cast<ImTextureID>(
                    static_cast<std::uintptr_t>(texture_id)),
                ImVec2(static_cast<float>(grid_width),
                       static_cast<float>(grid_height)));
            ImGui::End();
        }

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

    // MPP owns GPU resources created against the GL context; release them
    // while that context is still current.
    overview.shutdown();
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
