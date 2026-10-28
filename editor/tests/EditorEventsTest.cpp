#define SDL_MAIN_HANDLED
#include "EditorEvents.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void pushMotion() {
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.x = 20;
    motion.motion.y = 30;
    require(SDL_PushEvent(&motion) == 1, "Could not queue mouse motion");
}
}
int main() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_EVENTS) != 0) return 1;
    int result = 0;
    try {
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        // A deterministic stand-in for dragging: mouse input continues to
        // arrive while the pump runs. Rendering must not wait for silence.
        constexpr std::size_t streamLength = 4096;
        pushMotion();
        std::size_t produced = 1;
        const auto handled = processEditorEvents([&](SDL_Event& event) {
            require(event.type == SDL_MOUSEMOTION, "Unexpected event");
            if (produced++ < streamLength) pushMotion();
        });
        std::cout << "Events processed before yielding a frame: " << handled << '\n';
        require(handled > 0 && handled < streamLength,
                "Rendering starves until the entire mouse-motion stream stops");
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

        // A pre-existing backlog must also yield, without dropping input.
        constexpr std::size_t backlog = 1024;
        for (std::size_t i = 0; i < backlog; ++i) pushMotion();
        auto drained = processEditorEvents([](SDL_Event&) {});
        require(drained > 0 && drained <= 256, "An event backlog monopolises a frame");
        while (drained < backlog) {
            const auto count = processEditorEvents([](SDL_Event&) {});
            require(count > 0, "Queued input was lost");
            drained += count;
        }
        require(drained == backlog, "Queued input was duplicated");

        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 32; ++frame)
            require(processEditorEvents([](SDL_Event&) {}) == 0, "Idle pump fabricated events");
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "32 idle event pumps: " << elapsed << " ms\n";
        require(elapsed < 200, "An empty event queue blocks every frame");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
