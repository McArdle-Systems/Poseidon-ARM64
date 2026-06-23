# Third-Party Dependencies

Vendored libraries and headers included directly in the build.

| Directory | License | Description |
|-----------|---------|-------------|
| `glad/` | Generated | OpenGL 4.5 Core loader |
| `renderdoc/` | MIT | RenderDoc in-application API header |
| `metal-cpp/` | Apache-2.0 | Apple's official header-only C++ bindings for Metal/Foundation/QuartzCore (macOS backend), pinned to `release/metal-cpp_macOS26.4_iOS26.4` from [apple/metal-cpp](https://github.com/apple/metal-cpp) |

Additional dependencies managed via **vcpkg** (see `vcpkg.json`):
catch2, cli11, stb, mimalloc, freetype, sdl3, openal-soft, opus, enkits, imgui, spdlog.
