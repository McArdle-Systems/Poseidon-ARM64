#pragma once

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <PoseidonMTL/TextureMTL.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>

namespace Poseidon
{

class EngineMTLBootstrap;

// Real Metal-backed texture bank. Load-once-keep-forever (no LRU/memory
// budget -- see TextureMTL.hpp for why that's an acceptable simplification
// right now).
class TextBankMTL : public AbstractTextBank
{
  public:
    explicit TextBankMTL(EngineMTLBootstrap* bootstrap) : _bootstrap(bootstrap) {}
    ~TextBankMTL() override;

    int Find(RStringB name) const;

    Ref<Texture> Load(RStringB name) override;
    Ref<Texture> LoadInterpolated(RStringB /*n1*/, RStringB /*n2*/, float /*factor*/) override { return nullptr; }
    MipInfo UseMipmap(Texture* texture, int level, int /*levelTop*/) override { return MipInfo(texture, level); }

    void Compact() override {}
    void Preload() override {}
    void FlushTextures() override {}
    void FlushBank(QFBank* /*bank*/) override {}
    void ReleaseAllTextures() override { _texture.Clear(); }

    int NTextures() const override { return _texture.Size(); }
    Texture* GetTexture(int i) const override { return _texture[i]; }

  private:
    EngineMTLBootstrap* _bootstrap;
    LLinkArray<TextureMTL> _texture;
};

} // namespace Poseidon
