#include "OverviewRenderer.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    // Real offscreen-target readback. A desktop GL context is required; the
    // geometry test remains available on machines without a display server.
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 77;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("Overview render test", 0, 0, 64, 64,
                                         SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context) {
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }

    int result = 0;
    OverviewRenderer renderer;
    try {
        using BindTexture = void (APIENTRY *)(GLenum, GLuint);
        using GetTexImage = void (APIENTRY *)(GLenum, GLint, GLenum, GLenum, void*);
        const auto bind = reinterpret_cast<BindTexture>(SDL_GL_GetProcAddress("glBindTexture"));
        const auto read = reinterpret_cast<GetTexImage>(SDL_GL_GetProcAddress("glGetTexImage"));
        require(bind && read, "Texture readback functions unavailable");
        renderer.initialise(64, 64);
        auto pixels = [&](const station::editor::Installation& station, int width, int height) {
            const auto texture = renderer.render(width, height, station);
            std::vector<unsigned char> data(width * height * 4);
            bind(GL_TEXTURE_2D, texture);
            read(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
            return data;
        };
        int frame = 0;
        auto checkAll = [&](const auto& data, int red, int green, int blue) {
            ++frame;
            for (std::size_t i = 0; i < data.size(); i += 4) {
                if (std::abs(int(data[i]) - red) > 1 ||
                    std::abs(int(data[i + 1]) - green) > 1 ||
                    std::abs(int(data[i + 2]) - blue) > 1) {
                    throw std::runtime_error("Frame " + std::to_string(frame) + " unexpected pixel " + std::to_string(i / 4) +
                        ": got " + std::to_string(data[i]) + "," +
                        std::to_string(data[i + 1]) + "," + std::to_string(data[i + 2]) +
                        "; expected " + std::to_string(red) + "," +
                        std::to_string(green) + "," + std::to_string(blue));
                }
            }
        };
        using namespace station::editor;
        Installation empty({Level(2, 2, SectorType::Void)});
        // Start empty, grow, clear, and grow again to exercise batch updates.
        checkAll(pixels(empty, 64, 64), 20, 20, 20);
        checkAll(pixels(Installation::createDefault(), 64, 64), 150, 200, 235);
        checkAll(pixels(empty, 64, 64), 20, 20, 20);
        checkAll(pixels(Installation::createDefault(), 32, 32), 150, 200, 235);

        Installation sparse({Level(2, 2, SectorType::Void)});
        sparse.setCell(0, 0, 0, SectorType::Interior);
        const auto data = pixels(sparse, 32, 32);
        std::size_t floor_pixels = 0;
        for (std::size_t i = 0; i < data.size(); i += 4) {
            if (data[i] == 150 && data[i + 1] == 200 && data[i + 2] == 235) {
                ++floor_pixels;
            } else {
                require(std::abs(int(data[i]) - 20) <= 1 &&
                            std::abs(int(data[i + 1]) - 20) <= 1 &&
                            std::abs(int(data[i + 2]) - 20) <= 1,
                        "Void contains non-background pixels");
            }
        }
        require(floor_pixels == 16 * 16, "One Interior Cell must cover one solid tile");
        // Texture rows start at the bottom; ImGui flips the UVs for display.
        require(data[(31 * 32) * 4] == 150 && data[0] == 20,
                "Cell (0,0) must appear at the top-left after the ImGui UV flip");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    renderer.shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
