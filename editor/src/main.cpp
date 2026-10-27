// Editor application shell.
//
// A full-viewport host frame owns the menubar (File > Exit), an empty toolbar
// strip and a "Ready" statusbar; a DockSpace between them holds the single
// "Editor" panel docked to the left. The only functionality is exiting.

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder* (initial dock layout) is internal API
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"

#include <SDL.h>

#include <algorithm>
#include <filesystem>

namespace {

constexpr int kInitialWindowWidth = 1280;
constexpr int kInitialWindowHeight = 800;
constexpr float kInitialLeftDockWidthPx = 500.0f;
constexpr const char* kPanelTitle = "Editor";

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

int main() {
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

    while (running) {
        SDL_Event event;
        while (SDL_WaitEventTimeout(&event, 16) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }
        if (!running) {
            break;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

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
                if (ImGui::MenuItem("Exit")) {
                    running = false;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Toolbar: a fixed-height framed strip, empty until tools exist.
        const ImVec2 toolbar_min = ImGui::GetCursorScreenPos();
        const float toolbar_width = ImGui::GetContentRegionAvail().x;
        const float toolbar_height = ImGui::GetFrameHeight();
        ImGui::Dummy(ImVec2(toolbar_width, toolbar_height));
        ImGui::GetWindowDrawList()->AddRect(
            toolbar_min,
            ImVec2(toolbar_min.x + toolbar_width, toolbar_min.y + toolbar_height),
            ImGui::GetColorU32(ImGuiCol_Border));

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
        ImGui::TextUnformatted("Ready");
        ImGui::End();

        // --- The Editor panel ----------------------------------------------
        // p_open is null, so the panel has no close button and cannot be
        // closed into an empty application.
        ImGui::Begin(kPanelTitle);
        // Layouts restored from an older imgui.ini may still carry a tab bar;
        // re-assert the flag so the panel always renders as a plain window.
        if (ImGuiWindow* panel = ImGui::GetCurrentWindow();
            panel->DockNode != nullptr) {
            panel->DockNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        }
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
