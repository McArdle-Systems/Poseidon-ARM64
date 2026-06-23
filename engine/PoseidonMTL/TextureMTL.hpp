#pragma once

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Foundation/Types/LLinks.hpp>

namespace Poseidon
{

class EngineMTLBootstrap;

// Real Metal-backed texture: decodes the full top-level image via the shared
// PAADecoder utility (handles every PAA/PAC source format -- DXT1/3/5,
// ARGB8888/4444/1555, AI88, P8 -- decompressing to RGBA8888 on the CPU,
// since Apple Silicon GPUs have no BC/DXT hardware decode) and uploads it as
// a single-mip RGBA8Unorm MTLTexture via EngineMTLBootstrap::CreateTexture.
//
// Deliberately simpler than TextureGL33: one mip level (no mip chain -- menu/
// UI textures render close to 1:1), no LRU eviction/memory budget (load once,
// keep forever). Good enough for menu-scale texture counts; revisit if/when
// this backend needs to stream 3D world textures.
class TextureMTL : public Texture
{
  public:
    TextureMTL() = default;

    // Reads Name() through the VFS (GFileServer, so PBO-packed textures
    // work), decodes via DecodePAABuffer, and uploads via `bootstrap`.
    // Returns false (object stays valid, just renders as opaque white) on
    // any failure -- read error, decode failure, or GPU upload failure.
    bool LoadPixels(EngineMTLBootstrap& bootstrap);

    int GpuHandle() const { return _gpuHandle; }

    void SetMaxSize(int /*maxSize*/) override {}
    int AMaxSize() const override { return 4096; }

    int AWidth(int /*level*/ = 0) const override { return _w; }
    int AHeight(int /*level*/ = 0) const override { return _h; }
    int ANMipmaps() const override { return _gpuHandle != 0 ? 1 : 0; }
    void ASetNMipmaps(int /*n*/) override {}
    AbstractMipmapLevel& AMipmap(int /*level*/) override { return _mip; }
    const AbstractMipmapLevel& AMipmap(int /*level*/) const override { return _mip; }

    // Not used by the 2D draw path (textures are sampled on the GPU); only
    // here to satisfy the pure virtual. Real per-texel readback (e.g. for
    // gameplay queries) isn't implemented yet.
    Color GetPixel(int /*level*/, float /*u*/, float /*v*/) const override { return HBlack; }

    bool IsTransparent() const override { return _isTransparent; }
    bool IsAlpha() const override { return _isAlpha; }
    Color GetColor() override { return HBlack; }

    bool VerifyChecksum(const MipInfo& /*mip*/) const override { return true; }

  private:
    int _w = 0, _h = 0;
    int _gpuHandle = 0; // EngineMTLBootstrap texture handle; 0 = none/failed (renders fallback white)
    bool _isAlpha = false;
    bool _isTransparent = false;
    PacLevelMem _mip; // unused placeholder -- AMipmap() must return a reference to something
};

} // namespace Poseidon
