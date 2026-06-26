#include <PoseidonMTL/TextBankMTL.hpp>

#include <Poseidon/Foundation/Logging/Logging.hpp>

namespace Poseidon
{

TextBankMTL::~TextBankMTL()
{
    UnlockAllTextures();
    DeleteAllAnimated();
    // Individual GPU textures are not explicitly released here --
    // EngineMTLBootstrap::Shutdown() (called right after this destructor, by
    // EngineMTL's teardown order) releases every texture it owns
    // unconditionally.
}

int TextBankMTL::Find(RStringB name) const
{
    for (int i = 0; i < _texture.Size(); i++)
    {
        TextureMTL* texture = _texture[i];
        if (texture && texture->GetName() == name)
            return i;
    }
    return -1;
}

Ref<Texture> TextBankMTL::Load(RStringB name)
{
    int index = Find(name);
    if (index >= 0)
        return (Texture*)_texture[index];

    TextureMTL* texture = new TextureMTL();
    texture->SetName(name);
    texture->LoadPixels(*_bootstrap); // false on failure -- texture stays valid, renders as fallback white

    int iFree = _texture.Add();
    _texture[iFree] = texture;
    return texture;
}

MipInfo TextBankMTL::UseMipmap(Texture* texture, int level, int levelTop)
{
    if (texture == nullptr)
        return MipInfo(nullptr, 0);

    TextureMTL* mtlTexture = static_cast<TextureMTL*>(texture);
    const int selectedLevel = mtlTexture->NoteMipmapUse(level, levelTop);
    return MipInfo(texture, selectedLevel);
}

Texture* TextBankMTL::CreateDynamic(int w, int h, const void* rgba, uint32_t /*size*/, bool /*mipmap*/)
{
    // No mip-chain support (TextureMTL is single-mip by design); `mipmap` is
    // accepted to match the interface but ignored, same simplification
    // LoadPixels already makes.
    TextureMTL* texture = new TextureMTL();
    if (!texture->InitFromRGBA(*_bootstrap, w, h, rgba))
    {
        LOG_WARN(Graphics, "MTL: failed to create dynamic texture {}x{}", w, h);
        delete texture;
        return nullptr;
    }

    int iFree = _texture.Add();
    _texture[iFree] = texture;
    return texture;
}

void TextBankMTL::UpdateDynamic(Texture* texture, const void* rgba, uint32_t /*size*/)
{
    if (texture == nullptr)
        return;
    static_cast<TextureMTL*>(texture)->UpdateRGBA(*_bootstrap, rgba);
}

void TextBankMTL::FinishFrame()
{
    for (int i = 0; i < _texture.Size(); i++)
    {
        TextureMTL* texture = _texture[i];
        if (texture)
            texture->FinishFrameUseTracking();
    }
}

} // namespace Poseidon
