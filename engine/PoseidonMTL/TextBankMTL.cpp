#include <PoseidonMTL/TextBankMTL.hpp>

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

} // namespace Poseidon
