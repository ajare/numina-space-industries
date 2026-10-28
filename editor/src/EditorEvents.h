#pragma once
#include <SDL.h>
#include <cstddef>

// Shared by the application and the headless event-loop regression test.
template<class Handler>
std::size_t processEditorEvents(Handler&& handler) {
    std::size_t count = 0;
    SDL_Event event;
    // Never wait for a quiet interval: a continuous drag can keep supplying
    // events faster than that interval and prevent rendering indefinitely.
    // Bound even an already-full queue so input cannot monopolise a frame.
    constexpr std::size_t maxEventsPerFrame = 256;
    while (count < maxEventsPerFrame && SDL_PollEvent(&event) != 0) {
        handler(event);
        ++count;
    }
    return count;
}
