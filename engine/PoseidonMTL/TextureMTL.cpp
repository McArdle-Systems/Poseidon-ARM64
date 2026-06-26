#include <PoseidonMTL/TextureMTL.hpp>
#include <PoseidonMTL/EngineMTLBootstrap.hpp>

#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/Graphics/Textures/PAADecoder.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace Poseidon
{
namespace
{

AlphaStats::Kind ClassifyMetalAlpha(const AlphaStats& decoded)
{
    // Many OFP-era vehicle/cutout textures have hard transparent holes plus
    // a small band of antialias/compression alpha around the edge. Treating
    // any modest partial-alpha population as true translucent Blend makes
    // those panels paint over already-drawn interior geometry instead of
    // occluding it. Keep genuinely partial surfaces (glass/smoke/fades) in
    // Blend, but route hole-heavy textures with limited partial edge pixels
    // through the cutout path.
    if (decoded.pctClear >= 2.0 && decoded.pctPartial < 20.0)
        return AlphaStats::Cutout;
    return decoded.kind;
}

// Shared by LoadPixels and LoadPixelsInterpolated: read a PAA/PAC file
// through the VFS and decode every mip level it stores.
bool ReadAndDecodeChain(RStringB name, DecodedImageChain& chain)
{
    QIFStream in;
    GFileServer->Open(in, name);
    if (in.fail())
    {
        LOG_WARN(Graphics, "MTL: texture not found: {}", static_cast<const char*>(name));
        return false;
    }

    const int size = in.rest();
    if (size <= 0)
        return false;

    std::vector<char> fileData(static_cast<size_t>(size));
    in.read(fileData.data(), size);

    const char* cname = name;
    const size_t len = cname ? std::strlen(cname) : 0;
    const bool isPaa = len >= 4 && (cname[len - 1] == 'a' || cname[len - 1] == 'A'); // .paa vs .pac

    chain = DecodePAABufferAllMips(fileData.data(), static_cast<size_t>(size), isPaa);
    return chain.valid();
}

} // namespace

bool TextureMTL::LoadPixels(EngineMTLBootstrap& bootstrap)
{
    DecodedImageChain chain;
    if (!ReadAndDecodeChain(Name(), chain))
    {
        LOG_WARN(Graphics, "MTL: texture decode failed: {}", Name());
        return false;
    }
    const DecodedImage& top = chain.levels[0];

    _w = top.width;
    _h = top.height;
    _nMipmaps = static_cast<int>(chain.levels.size());
    _largestUsed = 0;
    _levelNeededThisFrame = _levelNeededLastFrame = _nMipmaps;
    _levels.clear();
    _levels.reserve(chain.levels.size());
    for (const DecodedImage& level : chain.levels)
        _levels.push_back({level.width, level.height});

    // Same tiering TextureGL33::GetAlphaClass() uses (ClassifyTextureAlpha's
    // documented policy): only decode-scan the alpha channel when the
    // SOURCE FORMAT can actually carry partial alpha. Unconditionally
    // running ClassifyAlpha on the decoded buffer (the previous bug here)
    // misclassified ordinary diffuse-only textures -- formats with no real
    // alpha channel commonly decode to a meaningless non-255 constant in
    // that byte (e.g. ijeepmg.paa decoded ~69% alpha=0), which made them
    // wrongly blend instead of render opaque.
    AlphaStats decoded;
    const AlphaStats* decodedPtr = nullptr;
    if (chain.hasAlphaChannel && !chain.oneBitAlpha)
    {
        decoded = ClassifyAlpha(top.rgba.data(), static_cast<size_t>(_w) * static_cast<size_t>(_h));
        decoded.kind = ClassifyMetalAlpha(decoded);
        decodedPtr = &decoded;
    }
    const AlphaStats::Kind kind =
        ClassifyTextureAlpha(chain.hasAlphaChannel, chain.isChromaKey, chain.oneBitAlpha, decodedPtr);
    _isAlpha = kind != AlphaStats::Opaque;
    _isTransparent = kind == AlphaStats::Cutout;

    // Real GPU mip chain (not just the top level) -- see CreateTextureMipped's
    // doc comment (EngineMTLBootstrap.hpp) for why a single-level texture
    // aliases at a distance. v1 still loads every level up front, same as
    // the single-level path before it; no per-frame visible-level streaming
    // yet (that's GL33's TextBankGL33, a separate, much larger piece).
    std::vector<EngineMTLBootstrap::MipLevel> levels;
    levels.reserve(chain.levels.size());
    for (const DecodedImage& level : chain.levels)
        levels.push_back({level.width, level.height, level.rgba.data()});

    _gpuHandle = bootstrap.CreateTextureMipped(levels.data(), static_cast<int>(levels.size()));
    if (_gpuHandle == 0)
    {
        LOG_WARN(Graphics, "MTL: texture GPU upload failed: {}", Name());
        return false;
    }
    // Only the top level is kept CPU-side -- GetPixel/GetColor (below) only
    // ever sample it, mirroring the original single-level behavior; storing
    // every level here would burn ~33% more host RAM for callers that don't
    // exist yet.
    _rgba = top.rgba;
    return true;
}

bool TextureMTL::LoadPixelsInterpolated(EngineMTLBootstrap& bootstrap, RStringB n1, RStringB n2, float factor)
{
    DecodedImageChain chain1, chain2;
    if (!ReadAndDecodeChain(n1, chain1) || !ReadAndDecodeChain(n2, chain2))
    {
        LOG_WARN(Graphics, "MTL: interpolated texture decode failed: {} / {}", static_cast<const char*>(n1),
                 static_cast<const char*>(n2));
        return false;
    }

    const size_t levelCount = std::min(chain1.levels.size(), chain2.levels.size());
    std::vector<std::vector<uint8_t>> blended;
    blended.reserve(levelCount);
    for (size_t i = 0; i < levelCount; i++)
    {
        const DecodedImage& a = chain1.levels[i];
        const DecodedImage& b = chain2.levels[i];
        if (a.width != b.width || a.height != b.height)
            break; // mismatched mip dims between the two sources -- stop, keep levels blended so far
        std::vector<uint8_t> out(a.rgba.size());
        for (size_t p = 0; p < out.size(); p++)
            out[p] = static_cast<uint8_t>(a.rgba[p] + (static_cast<int>(b.rgba[p]) - static_cast<int>(a.rgba[p])) * factor);
        blended.push_back(std::move(out));
    }
    if (blended.empty())
    {
        LOG_WARN(Graphics, "MTL: interpolated texture {} / {} share no compatible mip level",
                 static_cast<const char*>(n1), static_cast<const char*>(n2));
        return false;
    }

    _w = chain1.levels[0].width;
    _h = chain1.levels[0].height;
    _nMipmaps = static_cast<int>(blended.size());
    _largestUsed = 0;
    _levelNeededThisFrame = _levelNeededLastFrame = _nMipmaps;
    _levels.clear();
    _levels.reserve(blended.size());
    for (size_t i = 0; i < blended.size(); i++)
        _levels.push_back({chain1.levels[i].width, chain1.levels[i].height});

    // n1's alpha classification wins (matches GL33's Copy(index1) basing the
    // interpolated texture's identity on n1) -- weather sky textures are
    // diffuse-only opaque art in practice, but this keeps the same policy
    // LoadPixels uses instead of silently assuming Opaque.
    AlphaStats decoded;
    const AlphaStats* decodedPtr = nullptr;
    if (chain1.hasAlphaChannel && !chain1.oneBitAlpha)
    {
        decoded = ClassifyAlpha(blended[0].data(), static_cast<size_t>(_w) * static_cast<size_t>(_h));
        decoded.kind = ClassifyMetalAlpha(decoded);
        decodedPtr = &decoded;
    }
    const AlphaStats::Kind kind =
        ClassifyTextureAlpha(chain1.hasAlphaChannel, chain1.isChromaKey, chain1.oneBitAlpha, decodedPtr);
    _isAlpha = kind != AlphaStats::Opaque;
    _isTransparent = kind == AlphaStats::Cutout;

    std::vector<EngineMTLBootstrap::MipLevel> levels;
    levels.reserve(blended.size());
    for (size_t i = 0; i < blended.size(); i++)
        levels.push_back({chain1.levels[i].width, chain1.levels[i].height, blended[i].data()});

    _gpuHandle = bootstrap.CreateTextureMipped(levels.data(), static_cast<int>(levels.size()));
    if (_gpuHandle == 0)
    {
        LOG_WARN(Graphics, "MTL: interpolated texture GPU upload failed: {} / {}", static_cast<const char*>(n1),
                 static_cast<const char*>(n2));
        return false;
    }
    _rgba = blended[0];
    return true;
}

bool TextureMTL::InitFromRGBA(EngineMTLBootstrap& bootstrap, int w, int h, const void* rgba)
{
    if (rgba == nullptr)
        return false;

    _w = w;
    _h = h;
    _nMipmaps = 1;
    _largestUsed = 0;
    _levelNeededThisFrame = _levelNeededLastFrame = _nMipmaps;
    _levels.clear();
    _levels.push_back({w, h});
    _gpuHandle = bootstrap.CreateTexture(w, h, static_cast<const uint8_t*>(rgba));
    if (_gpuHandle == 0)
        return false;
    const auto* bytes = static_cast<const uint8_t*>(rgba);
    _rgba.assign(bytes, bytes + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    return true;
}

void TextureMTL::UpdateRGBA(EngineMTLBootstrap& bootstrap, const void* rgba)
{
    if (_gpuHandle == 0 || rgba == nullptr)
        return;
    bootstrap.UpdateTexture(_gpuHandle, _w, _h, static_cast<const uint8_t*>(rgba));
    const auto* bytes = static_cast<const uint8_t*>(rgba);
    _rgba.assign(bytes, bytes + static_cast<size_t>(_w) * static_cast<size_t>(_h) * 4);
}

int TextureMTL::LevelWidth(int level) const
{
    if (!_levels.empty())
    {
        level = std::clamp(level, 0, static_cast<int>(_levels.size()) - 1);
        return _levels[static_cast<size_t>(level)].width;
    }
    level = std::max(level, 0);
    return std::max(1, _w >> level);
}

int TextureMTL::LevelHeight(int level) const
{
    if (!_levels.empty())
    {
        level = std::clamp(level, 0, static_cast<int>(_levels.size()) - 1);
        return _levels[static_cast<size_t>(level)].height;
    }
    level = std::max(level, 0);
    return std::max(1, _h >> level);
}

int TextureMTL::AWidth(int level) const
{
    return LevelWidth(level);
}

int TextureMTL::AHeight(int level) const
{
    return LevelHeight(level);
}

void TextureMTL::SetMipmapRange(int min, int max)
{
    const int available = _levels.empty() ? std::max(_nMipmaps, 1) : static_cast<int>(_levels.size());
    if (available <= 0)
    {
        _nMipmaps = 0;
        _largestUsed = 0;
        return;
    }

    min = std::clamp(min, 0, available - 1);
    max = std::clamp(max, 0, available - 1);
    if (min > max)
        min = max;
    _largestUsed = min;
    _nMipmaps = max + 1;
}

int TextureMTL::NoteMipmapUse(int level, int levelTop)
{
    if (_nMipmaps <= 0)
        return -1;

    if (level < 0)
        level = 0;
    level = std::min(level, _nMipmaps - 1);
    levelTop = std::max(levelTop, _largestUsed);
    levelTop = std::min(levelTop, level);
    level = std::max(level, levelTop);

    level = std::min(level, _mipmapNeeded);
    levelTop = std::min(levelTop, _mipmapWanted);
    if (level < 0)
        level = 0;
    level = std::min(level, _nMipmaps - 1);
    levelTop = std::max(levelTop, _largestUsed);
    levelTop = std::min(levelTop, level);
    level = std::max(level, levelTop);

    if (_levelNeededThisFrame > level)
        _levelNeededThisFrame = level;

    return level;
}

void TextureMTL::FinishFrameUseTracking()
{
    _levelNeededLastFrame = _levelNeededThisFrame;
    _levelNeededThisFrame = _nMipmaps;
    ResetMipmap();
}

Color TextureMTL::GetPixel(int /*level*/, float u, float v) const
{
    if (_rgba.empty() || _w <= 0 || _h <= 0)
        return HBlack;

    int iu = static_cast<int>(std::floor(u * static_cast<float>(_w)));
    int iv = static_cast<int>(std::floor(v * static_cast<float>(_h)));
    if (iu < 0)
        iu = 0;
    if (iv < 0)
        iv = 0;
    if (iu > _w - 1)
        iu = _w - 1;
    if (iv > _h - 1)
        iv = _h - 1;

    const size_t idx = (static_cast<size_t>(iv) * static_cast<size_t>(_w) + static_cast<size_t>(iu)) * 4;
    return Color(_rgba[idx] / 255.0f, _rgba[idx + 1] / 255.0f, _rgba[idx + 2] / 255.0f, _rgba[idx + 3] / 255.0f);
}

Color TextureMTL::GetColor()
{
    if (_rgba.empty())
        return HBlack;

    const size_t pixelCount = _rgba.size() / 4;
    double r = 0, g = 0, b = 0, a = 0;
    for (size_t i = 0; i < pixelCount; i++)
    {
        r += _rgba[i * 4];
        g += _rgba[i * 4 + 1];
        b += _rgba[i * 4 + 2];
        a += _rgba[i * 4 + 3];
    }
    return Color(static_cast<float>(r / pixelCount / 255.0), static_cast<float>(g / pixelCount / 255.0),
                static_cast<float>(b / pixelCount / 255.0), static_cast<float>(a / pixelCount / 255.0));
}

} // namespace Poseidon
