# Metal Port — Progress Notes (WIP)

Status as of this commit. Written so a future session (or another contributor)
can pick this up without re-deriving context.

## Where things stand

**The main menu renders and is interactive, on native Metal, on Apple
Silicon, against real retail game data.** Confirmed by hand: launched the
game, the menu appeared, clicked a button, a settings (sub-)menu opened.
This is the milestone the whole session was aimed at.

Earlier in this session the window appeared solid black; debug
instrumentation (left in place per request -- see `DEBUG` log lines in
`EngineMTL::Clear`/`Draw2D`) proved real draw calls with real textures/colors
were reaching the GPU even then. The black screen turned out to not be the
final state -- once actually interacted with, the real menu was there
underneath/after. Root cause of the initial black appearance wasn't
conclusively isolated (candidates: a loading-screen fade, or the visible
content simply needing a frame/input nudge) -- worth a closer look next
session, but no longer blocking.

```
config → Metal device init (Apple M-series) → display/graphics config
  → asset banks mounted (packages/Remaster) → landscape loaded
  → fonts initialized → scene preloader initialized
  → enters World::Simulate / RenderFrame, runs continuously (no crash)
  → real Draw2D / DrawPoly / 3D-mesh-fan draw calls confirmed flowing
    through the Metal pipeline with live data (debug logging left in,
    see EngineMTL::Clear/Draw2D)
  → main menu renders and is interactive (confirmed by hand)
```

## Debug logging currently left in (intentional, per request)

- `EngineMTL::Clear`: logs `IsPipelineReady()` once, and the first 5
  `BeginFrame()` failures if any.
- `EngineMTL::Draw2D`: logs rect/texture-handle/colorTL for the first 40 calls.
- `EngineMTLBootstrap::IsPipelineReady()`: new accessor backing the above --
  added because this class's own error logging goes through raw
  `fprintf(stderr, ...)` (it can't include Poseidon's `LOG_*` macros --
  metal-cpp/Poseidon header collision, see the class comment), and a host
  process redirecting/capturing stderr could silently swallow those
  messages. Worth remembering if a *real* shader-compile/pipeline-creation
  failure ever needs diagnosing: check `IsPipelineReady()` via `LOG_*` from
  EngineMTL, don't trust the absence of an `EngineMTLBootstrap: ...` stderr
  line as proof nothing failed.

Strip all of the above once the black-screen-at-startup question is closed
out and the menu's visual correctness (colors, text, art) is verified.

## Milestones completed

### Milestone 0 — bare Metal pipeline (`engine/PoseidonMTL/EngineMTLBootstrap.*`)
Standalone proof that SDL3 → `CAMetalLayer` → `MTLDevice` → clear → present
works at all. Exercised by `apps/tools/MetalSmokeTest`. No Poseidon engine
dependency — deliberately isolated because metal-cpp's `Foundation.hpp` and
Poseidon's core headers can't be included in the same translation unit (see
`EngineMTLBootstrap.hpp`'s top comment for why).

### Milestone 1 — real `Poseidon::Engine` backend (`engine/PoseidonMTL/EngineMTL.*`)
`EngineMTL` implements the full `IGraphicsEngine`/`Engine` virtual contract
(~38 methods) and registers as backend `"mtl"` via
`GraphicsEngineFactory`/`RegisterMetalGraphicsBackend()`, selectable with
`--render mtl` (macOS-only CLI option, added in `AppConfig.cpp`). Window
creation, display-mode switching, and event handling reuse the same
`WindowPlacement` resolver and `SDLEventWindow` helper GL33 uses.

### Milestone 1.5 — macOS/ARM64 portability fixes to Poseidon core
None of this is Metal-specific; it was blocking `Poseidon` (the engine core
library) from compiling on Apple Silicon **at all**, regardless of graphics
backend:

- `finite()` → `isfinite()`/`__builtin_isfinite` (macOS libc++ doesn't have
  the BSD/glibc `finite()` compat function). `platform.hpp`, `MathOpt.cpp`.
- x86 SSE/MMX intrinsics have no ARM64 equivalent. Vendored
  [sse2neon](https://github.com/DLTcollab/sse2neon) (`thirdparty/sse2neon/`)
  and added `engine/Poseidon/Foundation/Common/X86IntrinsicsCompat.hpp` as
  the single redirect point + the handful of legacy MMX intrinsics
  (`_mm_packs_pi32`, `_mm_packs_pu16`, `_mm_set1_pi8`, `_mm_cmpgt_pi8`,
  `_mm_and_si64`/`_mm_or_si64`/`_mm_andnot_si64`) sse2neon doesn't cover,
  hand-implemented against NEON matching Intel's documented semantics.
- Linux-only headers (`link.h`, `linux/sysinfo.h`, `malloc.h`) gated/replaced
  with macOS equivalents or stubs (`CrashHandler.cpp` build-ID capture,
  `MemGrow.cpp` OOM diagnostics via `sysctlbyname("vm.swapusage", ...)`).
- `PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP` (glibc-only) replaced with the
  portable `pthread_mutexattr_settype(PTHREAD_MUTEX_RECURSIVE)` form.
- `apps/cwr/Game/CMakeLists.txt` needed its own `elseif(APPLE)` branch --
  Apple's linker has no `--start-group`/`--end-group` equivalent.

### Milestone 2 — real 2D draw path + texture bank + minimal 3D mesh path
This is today's milestone. Three things landed together because the crash
chain forced the issue (see "Where it stops" history below):

**Real 2D rendering** (`EngineMTL::Draw2D`/`DrawPoly`/`DrawLine`,
`EngineMTLBootstrap::BeginFrame`/`DrawTriangles2D`/`EndFrame`): pixel-space
vertices convert to NDC on the CPU (no GPU matrix math needed for 2D), get
fanned into triangles, and draw through one embedded MSL shader (manual
vertex-id fetch, no `MTLVertexDescriptor` -- see `kShaderSource2D` in
`EngineMTLBootstrap.cpp`). Per-draw scissor rect replaces GL33's manual
UV-remapping clip math. Alpha blending is on (straight, non-premultiplied).

**Real texture bank** (`TextureMTL`/`TextBankMTL`): `Load()` reads bytes
through `GFileServer` (works for PBO-packed textures), decodes via the
existing `DecodePAABuffer` utility (handles every PAA/PAC source format,
decompressing DXT to RGBA8 on the CPU since Apple Silicon has no BC/DXT
hardware decode), and uploads as a single-mip `MTLTexture`. No mip chain, no
LRU eviction/budget -- load-once-keep-forever, fine for menu-scale texture
counts, revisit if/when this backend needs to stream 3D world textures.

**Minimal 3D mesh path** (`EngineMTL::PrepareTriangle`/`BeginMesh`/`EndMesh`/
`DrawPolygon`/`DrawSection`/`DrawLine(int,int)`): turned out to be required
even for the *menu* -- sky/cloud/sun/moon render as real 3D objects
unconditionally inside `Landscape::Draw()`, regardless of `--no-menu-scene`
(that flag only skips the intro camera fly-through). The key finding that
made this tractable: by the time the engine sees a `TLVertex`, the CPU
(`TLVertexTable`, see `TransLight.cpp`) has already done the full
model→view→projection→perspective-divide transform -- positions are already
screen-space pixels. So the 3D path needs no GPU matrix math either; it
reuses the *same* `DrawFan2D` helper as the 2D path, just sourcing vertices
from the bound `TLVertexTable` by index instead of `Vertex2DAbs`. No
lighting/shadows yet (flat per-vertex color). The hardware-TL path
(`PrepareMeshTL`/`BeginMeshTL`/`DrawSectionTL`, used for terrain/vehicles) is
a separate, not-yet-implemented milestone.

**Two pre-existing engine bugs fixed** (`engine/Poseidon/World/Terrain/LandscapeRender.cpp`,
not Metal-specific -- these would crash identically on Linux/Windows GL33
given the same data): `Landscape::DrawSky()` and `DrawHorizont()`
unconditionally dereferenced `_skyObject`/`_starsObject`/`_sunObject`/
`_moonObject`/`_horizontObject` even though the engine's own logging
("Landscape sky slot 'X' has no model configured") proves it already knows
these can be null when a landscape config omits a sky slot. `DrawClouds()`
right next to them already guarded its per-cloud objects the same way
(`if (!object) continue;`) -- `DrawSky`/`DrawHorizont` just never got the
same treatment. Added the missing null guards, matching the existing
defensive pattern instead of introducing a new one.

### Real-data validation
Tested against the user's actual retail `packages/Remaster` install (DTA/BIN/
AddOns/Missions/Worlds PBOs — Windows-only DLLs and the 99MB DirectX/OpenAL
installer redistributables stripped out, dead weight on macOS). Two genuine
data gaps found (both data, not code): missing
`fonts/{cwr_title,cwr_body,cwr_mono,cwr_serif,cwr_hand}.ttf` (loose files at
the content root, not packed in a PBO -- pulled from the official Remaster
Demo zip), and a missing vehicle texture (`biscamel\icamel2.paa`, logged as a
warning, not fatal). `packages/` is gitignored; nothing data-related is in
this repo.

## Where it stops

The app no longer crashes -- it runs the per-frame loop continuously. The
window renders **solid black**. Confirmed via temporary debug logging (added,
checked, then removed before this commit) that real draw calls *are*
reaching the Metal pipeline with live data:

- 3D mesh fan draws: real `TLVertexTable` data, real non-zero texture handle.
- 2D UI draws (`Draw2D`): real rects (including two very large ones —
  `33600×12150` at large negative coordinates, consistent with letterbox/
  pillarbox bars), but **texture handle 0** on all of them, meaning the
  underlying `TextureMTL::LoadPixels()` did not succeed for those textures.

Handle 0 is *not* "render nothing" -- `DrawTriangles2D` substitutes an opaque
white 1×1 fallback texture for handle 0, multiplied by the per-vertex color.
So a failed texture load alone should show as a flat white/vertex-colored
quad, not black. For these to render black, the vertex color itself
(`Draw2DPars::colorTL/TR/BL/BR`) would have to be black or near-black, which
wasn't logged this session. **Next step: log the actual color values (not
just texture handle) for these draws**, and separately check why those
specific textures are failing to load (could be missing assets, like the
fonts/vehicle-texture gaps already found, or a real bug in `TextureMTL`).
Also worth a sanity check: is this a brief black fade-in transition that
brightens after more frames, or genuinely stuck black indefinitely? Watch
longer than a few seconds before concluding it's a bug.

## Next milestone (not started)

Once the black-screen question is resolved:
- Lighting (the per-vertex `TLVertex.color` is already CPU-computed and
  flowing through correctly -- this may turn out to need no extra GPU work)
- MSL shader equivalents of the 9 GLSL sources in
  `engine/PoseidonGL33/EngineGL33_Shaders.cpp` for the *hardware*-TL path
  (40 linked-program permutations there; worth spiking whether SPIRV-Cross
  can semi-automate GLSL→SPIR-V→MSL before hand-porting)
- Map the existing 3 UBOs (VS constants, PS constants, WorldInstances) onto
  Metal's buffer-index binding model for that hardware-TL path
- Shadow maps / SSAA / instancing are later still — see the original M0 plan
  roadmap for the full GL33 feature surface still to match

## Key files

| File | Purpose |
|------|---------|
| `engine/PoseidonMTL/EngineMTL.{hpp,cpp}` | The real `Poseidon::Engine` backend -- 2D + minimal 3D mesh draw path |
| `engine/PoseidonMTL/EngineMTLBootstrap.{hpp,cpp}` | Metal device/layer/queue/pipeline/texture wrapper (Engine-agnostic, metal-cpp-isolated) |
| `engine/PoseidonMTL/TextureMTL.{hpp,cpp}` / `TextBankMTL.{hpp,cpp}` | Real PAA decode + GPU texture upload |
| `engine/PoseidonMTL/GraphicsBackendMTL.cpp` | Factory registration (`"mtl"`) |
| `engine/PoseidonMTL/MetalCppImpl.cpp` | metal-cpp's `*_PRIVATE_IMPLEMENTATION` macros (one definition per binary) |
| `engine/Poseidon/Foundation/Common/X86IntrinsicsCompat.hpp` | x86 SIMD → sse2neon redirect + MMX shims |
| `engine/Poseidon/World/Terrain/LandscapeRender.cpp` | `DrawSky`/`DrawHorizont` null-guard fixes (pre-existing bug, not Metal-specific) |
| `apps/tools/MetalSmokeTest/` | Milestone-0 standalone smoke test |
