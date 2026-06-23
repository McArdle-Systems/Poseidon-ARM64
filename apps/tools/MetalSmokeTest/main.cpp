// Milestone-0 smoke test for the native Metal backend: opens a window and
// continuously presents frames cleared to a fixed, visually distinct color.
// No shaders/meshes/textures — just proves the SDL3 -> CAMetalLayer ->
// MTLDevice -> clear -> present pipeline works end to end on this machine.
#include <PoseidonMTL/EngineMTL.hpp>

#include <SDL3/SDL.h>

#include <cstdio>

int main(int, char**)
{
    Poseidon::EngineMTLBootstrap engine;
    if (!engine.Init("PoseidonMTL Smoke Test", 1280, 720))
    {
        std::fprintf(stderr, "Failed to initialize Metal bootstrap\n");
        return 1;
    }

    std::printf("Window open. Clearing to cornflower blue. Close the window or press Esc to quit.\n");

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
                running = false;
            else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                engine.OnWindowResized(event.window.data1, event.window.data2);
        }

        // Cornflower blue (100, 149, 237) in linear 0..1.
        engine.RenderClearAndPresent(100.0f / 255.0f, 149.0f / 255.0f, 237.0f / 255.0f, 1.0f);
    }

    engine.Shutdown();
    return 0;
}
