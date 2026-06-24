#include <PoseidonMTL/TextureMTL.hpp>
#include <PoseidonMTL/EngineMTLBootstrap.hpp>

#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/IO/Streams/QStream.hpp>
#include <Poseidon/Graphics/Textures/PAADecoder.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

#include <cstring>
#include <vector>

namespace Poseidon
{

bool TextureMTL::LoadPixels(EngineMTLBootstrap& bootstrap)
{
    QIFStream in;
    GFileServer->Open(in, Name());
    if (in.fail())
    {
        LOG_WARN(Graphics, "MTL: texture not found: {}", Name());
        return false;
    }

    const int size = in.rest();
    if (size <= 0)
        return false;

    std::vector<char> fileData(static_cast<size_t>(size));
    in.read(fileData.data(), size);

    const char* name = Name();
    const size_t len = name ? std::strlen(name) : 0;
    const bool isPaa = len >= 4 && (name[len - 1] == 'a' || name[len - 1] == 'A'); // .paa vs .pac

    const DecodedImage img = DecodePAABuffer(fileData.data(), static_cast<size_t>(size), isPaa);
    if (!img.valid())
    {
        LOG_WARN(Graphics, "MTL: texture decode failed: {}", name);
        return false;
    }

    _w = img.width;
    _h = img.height;

    const AlphaStats stats = ClassifyAlpha(img.rgba.data(), static_cast<size_t>(_w) * static_cast<size_t>(_h));
    _isAlpha = stats.kind != AlphaStats::Opaque;
    _isTransparent = stats.kind == AlphaStats::Cutout;

    _gpuHandle = bootstrap.CreateTexture(_w, _h, img.rgba.data());
    if (_gpuHandle == 0)
    {
        LOG_WARN(Graphics, "MTL: texture GPU upload failed: {}", name);
        return false;
    }
    return true;
}

bool TextureMTL::InitFromRGBA(EngineMTLBootstrap& bootstrap, int w, int h, const void* rgba)
{
    if (rgba == nullptr)
        return false;

    _w = w;
    _h = h;
    _gpuHandle = bootstrap.CreateTexture(w, h, static_cast<const uint8_t*>(rgba));
    return _gpuHandle != 0;
}

void TextureMTL::UpdateRGBA(EngineMTLBootstrap& bootstrap, const void* rgba)
{
    if (_gpuHandle == 0 || rgba == nullptr)
        return;
    bootstrap.UpdateTexture(_gpuHandle, _w, _h, static_cast<const uint8_t*>(rgba));
}

} // namespace Poseidon
