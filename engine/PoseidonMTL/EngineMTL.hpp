#pragma once

struct SDL_Window;

namespace Poseidon
{

// Milestone-0 scaffold for the native Metal backend (macOS / Apple Silicon).
// Proves SDL3 window -> CAMetalLayer -> MTLDevice -> clear -> present end to
// end. Deliberately NOT a Poseidon::Engine subclass yet: Engine
// (engine/Poseidon/Graphics/Core/Engine.hpp) is a large interface covering
// shadow maps, instancing, MSAA/SSAA, debug groups, etc. that belongs to a
// later milestone once this pipeline is validated.
class EngineMTLBootstrap
{
  public:
    EngineMTLBootstrap();
    ~EngineMTLBootstrap();

    EngineMTLBootstrap(const EngineMTLBootstrap&) = delete;
    EngineMTLBootstrap& operator=(const EngineMTLBootstrap&) = delete;

    // Creates the SDL_WINDOW_METAL window, attaches a CAMetalLayer, picks the
    // system default MTLDevice, and creates the command queue. Returns false
    // (with a message on stderr) on any failure.
    bool Init(const char* title, int width, int height);

    // Clears the next drawable to the given color (0..1 range) and presents
    // it. No render pass content beyond the clear — no shaders/meshes yet.
    void RenderClearAndPresent(float r, float g, float b, float a);

    void OnWindowResized(int width, int height);

    void Shutdown();

    SDL_Window* Window() const { return _window; }

  private:
    struct Impl;
    Impl* _impl = nullptr;
    SDL_Window* _window = nullptr;
};

} // namespace Poseidon
