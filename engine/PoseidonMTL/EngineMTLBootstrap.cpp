#include <PoseidonMTL/EngineMTLBootstrap.hpp>

// metal-cpp implementation macros live in MetalCppImpl.cpp (one definition
// per binary); this file only needs the declarations.
#include <Foundation/Foundation.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <cstdio>
#include <vector>

namespace Poseidon
{

namespace
{
// Manual vertex fetch by vertex_id (no MTLVertexDescriptor) -- the simplest
// correct setup for a single fixed vertex layout. Vertex2D's MSL layout
// (float2 + float2 + float4, natural alignment) matches Vertex2DMTL's C++
// layout byte-for-byte: position@0, uv@8, color@16, size 32.
const char* kShaderSource2D = R"(
#include <metal_stdlib>
using namespace metal;

struct Vertex2D {
    float2 position;
    float2 uv;
    float4 color;
};

struct VSOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex VSOut vs2d(uint vid [[vertex_id]], const device Vertex2D* verts [[buffer(0)]])
{
    Vertex2D v = verts[vid];
    VSOut out;
    out.position = float4(v.position, 0.0, 1.0);
    out.uv = v.uv;
    out.color = v.color;
    return out;
}

fragment float4 fs2d(VSOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler samp [[sampler(0)]])
{
    float4 texColor = tex.sample(samp, in.uv);
    return texColor * in.color;
}
)";
} // namespace

struct EngineMTLBootstrap::Impl
{
    SDL_MetalView metalView = nullptr;
    CA::MetalLayer* layer = nullptr;
    MTL::Device* device = nullptr;
    MTL::CommandQueue* commandQueue = nullptr;

    MTL::RenderPipelineState* pipelineState = nullptr;
    MTL::SamplerState* samplerState = nullptr;
    MTL::Texture* fallbackWhite = nullptr;
    std::vector<MTL::Texture*> textures; // handle = index + 1; 0 reserved for "none"

    // Open between BeginFrame/EndFrame.
    CA::MetalDrawable* currentDrawable = nullptr;
    MTL::CommandBuffer* currentCommandBuffer = nullptr;
    MTL::RenderCommandEncoder* currentEncoder = nullptr;

    int drawableWidth = 0;
    int drawableHeight = 0;
};

EngineMTLBootstrap::EngineMTLBootstrap() : _impl(new Impl()) {}

EngineMTLBootstrap::~EngineMTLBootstrap()
{
    Shutdown();
    delete _impl;
}

bool EngineMTLBootstrap::Init(const char* title, int width, int height)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "EngineMTLBootstrap: SDL_InitSubSystem(VIDEO) failed: %s\n", SDL_GetError());
        return false;
    }

    _window = SDL_CreateWindow(title, width, height, SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (_window == nullptr)
    {
        std::fprintf(stderr, "EngineMTLBootstrap: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    _ownsWindow = true;

    return SetupDevice();
}

bool EngineMTLBootstrap::AttachToWindow(SDL_Window* window)
{
    _window = window;
    _ownsWindow = false;
    return SetupDevice();
}

bool EngineMTLBootstrap::SetupDevice()
{
    _impl->metalView = SDL_Metal_CreateView(_window);
    if (_impl->metalView == nullptr)
    {
        std::fprintf(stderr, "EngineMTLBootstrap: SDL_Metal_CreateView failed: %s\n", SDL_GetError());
        return false;
    }

    _impl->layer = static_cast<CA::MetalLayer*>(SDL_Metal_GetLayer(_impl->metalView));
    if (_impl->layer == nullptr)
    {
        std::fprintf(stderr, "EngineMTLBootstrap: SDL_Metal_GetLayer returned null\n");
        return false;
    }

    _impl->device = MTL::CreateSystemDefaultDevice();
    if (_impl->device == nullptr)
    {
        std::fprintf(stderr, "EngineMTLBootstrap: MTL::CreateSystemDefaultDevice() returned null\n");
        return false;
    }

    _impl->layer->setDevice(_impl->device);
    _impl->layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);

    int pxWidth = 0, pxHeight = 0;
    SDL_GetWindowSizeInPixels(_window, &pxWidth, &pxHeight);
    _impl->layer->setDrawableSize(CGSizeMake(static_cast<CGFloat>(pxWidth), static_cast<CGFloat>(pxHeight)));
    _impl->drawableWidth = pxWidth;
    _impl->drawableHeight = pxHeight;

    _impl->commandQueue = _impl->device->newCommandQueue();
    if (_impl->commandQueue == nullptr)
    {
        std::fprintf(stderr, "EngineMTLBootstrap: newCommandQueue() returned null\n");
        return false;
    }

    return true;
}

void EngineMTLBootstrap::RenderClearAndPresent(float r, float g, float b, float a, bool clear)
{
    if (_impl->layer == nullptr || _impl->commandQueue == nullptr)
        return;

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    CA::MetalDrawable* drawable = _impl->layer->nextDrawable();
    if (drawable == nullptr)
    {
        pool->release();
        return;
    }

    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
    colorAttachment->setTexture(drawable->texture());
    colorAttachment->setLoadAction(clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);
    colorAttachment->setClearColor(MTL::ClearColor::Make(r, g, b, a));

    MTL::CommandBuffer* cmdBuf = _impl->commandQueue->commandBuffer();
    MTL::RenderCommandEncoder* encoder = cmdBuf->renderCommandEncoder(passDesc);
    encoder->endEncoding();

    cmdBuf->presentDrawable(drawable);
    cmdBuf->commit();

    passDesc->release();
    pool->release();
}

void EngineMTLBootstrap::OnWindowResized(int width, int height)
{
    if (_impl->layer == nullptr)
        return;
    _impl->drawableWidth = width;
    _impl->drawableHeight = height;
    _impl->layer->setDrawableSize(CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height)));
}

std::string EngineMTLBootstrap::GetRendererName() const
{
    if (_impl->device == nullptr)
        return {};
    return _impl->device->name()->utf8String();
}

bool EngineMTLBootstrap::IsPipelineReady() const
{
    return _impl->pipelineState != nullptr;
}

void EngineMTLBootstrap::EnsurePipeline()
{
    if (_impl->pipelineState != nullptr || _impl->device == nullptr)
        return;

    NS::Error* error = nullptr;
    NS::String* src = NS::String::string(kShaderSource2D, NS::StringEncoding::UTF8StringEncoding);
    MTL::Library* library = _impl->device->newLibrary(src, nullptr, &error);
    if (library == nullptr)
    {
        std::fprintf(stderr, "EngineMTLBootstrap: shader compile failed: %s\n",
                     error ? error->localizedDescription()->utf8String() : "(unknown)");
        return;
    }

    MTL::Function* vsFn = library->newFunction(NS::String::string("vs2d", NS::StringEncoding::UTF8StringEncoding));
    MTL::Function* fsFn = library->newFunction(NS::String::string("fs2d", NS::StringEncoding::UTF8StringEncoding));

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vsFn);
    desc->setFragmentFunction(fsFn);
    MTL::RenderPipelineColorAttachmentDescriptor* colorDesc = desc->colorAttachments()->object(0);
    colorDesc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    colorDesc->setBlendingEnabled(true);
    colorDesc->setRgbBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setAlphaBlendOperation(MTL::BlendOperationAdd);
    colorDesc->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
    colorDesc->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    colorDesc->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);

    _impl->pipelineState = _impl->device->newRenderPipelineState(desc, &error);
    if (_impl->pipelineState == nullptr)
    {
        std::fprintf(stderr, "EngineMTLBootstrap: pipeline state creation failed: %s\n",
                     error ? error->localizedDescription()->utf8String() : "(unknown)");
    }

    desc->release();
    vsFn->release();
    fsFn->release();
    library->release();

    MTL::SamplerDescriptor* sampDesc = MTL::SamplerDescriptor::alloc()->init();
    sampDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
    sampDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
    sampDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
    sampDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
    _impl->samplerState = _impl->device->newSamplerState(sampDesc);
    sampDesc->release();
}

void EngineMTLBootstrap::EnsureFallbackResources()
{
    if (_impl->fallbackWhite != nullptr || _impl->device == nullptr)
        return;

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);
    _impl->fallbackWhite = _impl->device->newTexture(desc);
    desc->release();

    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    _impl->fallbackWhite->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, whitePixel, 4);
}

bool EngineMTLBootstrap::BeginFrame(float r, float g, float b, float a, bool clear)
{
    if (_impl->layer == nullptr || _impl->commandQueue == nullptr)
        return false;

    EnsurePipeline();
    EnsureFallbackResources();
    if (_impl->pipelineState == nullptr)
        return false;

    // Local pool just to drain incidental temporaries from this call -- it
    // must NOT be relied on to keep the drawable/command buffer/encoder
    // alive past this function returning. Those three are stored in _impl
    // for DrawTriangles2D/EndFrame to use across separate later calls, but
    // commandQueue->commandBuffer() and commandBuffer->renderCommandEncoder()
    // (like nextDrawable()) return autoreleased objects, not owned (+1)
    // references -- draining ANY pool that was active when they were
    // created can deallocate them. That's exactly what an earlier version
    // of this function did (pool scoped to this call, released at the
    // bottom): Metal's own validation caught the encoder being deallocated
    // without endEncoding() ever having been called on it and called
    // abort() (SIGABRT, reproduced by selecting a main-menu item). Explicit
    // retain()/release() below -- the same manual-ownership pattern already
    // used for vbuf/ibuf/passDesc/textures in this file -- makes their
    // lifetime independent of pool timing entirely.
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    if (_impl->currentDrawable == nullptr)
    {
        // First Clear() of this displayed frame: acquire the drawable and
        // open the frame's one command buffer. This must happen exactly
        // once per frame -- nextDrawable() hands back a recycled texture
        // from CAMetalLayer's small swap pool (commonly 3 buffers), not
        // "the same buffer earlier draws this frame already went into".
        // Engine code legitimately calls Clear() more than once per frame
        // (e.g. UIContainers.cpp clears Z only, mid-frame, before a 3D
        // preview pass) -- calling nextDrawable() again on those calls used
        // to hand later draws a different, stale-content texture and
        // silently drop the in-flight command buffer from earlier in the
        // frame, which is what produced the ghosting/stale-content
        // artifacts.
        _impl->currentDrawable = _impl->layer->nextDrawable();
        if (_impl->currentDrawable == nullptr)
        {
            pool->release();
            return false;
        }
        _impl->currentDrawable->retain();
        _impl->currentCommandBuffer = _impl->commandQueue->commandBuffer();
        _impl->currentCommandBuffer->retain();
    }
    else if (_impl->currentEncoder != nullptr)
    {
        // Mid-frame Clear(): reuse the same drawable/command buffer, just
        // end the previous pass's encoder before opening a new one on it.
        _impl->currentEncoder->endEncoding();
        _impl->currentEncoder->release();
        _impl->currentEncoder = nullptr;
    }

    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* colorAttachment = passDesc->colorAttachments()->object(0);
    colorAttachment->setTexture(_impl->currentDrawable->texture());
    colorAttachment->setLoadAction(clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);
    colorAttachment->setClearColor(MTL::ClearColor::Make(r, g, b, a));

    _impl->currentEncoder = _impl->currentCommandBuffer->renderCommandEncoder(passDesc);
    _impl->currentEncoder->retain();
    _impl->currentEncoder->setRenderPipelineState(_impl->pipelineState);
    _impl->currentEncoder->setFragmentSamplerState(_impl->samplerState, 0);

    passDesc->release();
    pool->release();
    return true;
}

void EngineMTLBootstrap::DrawTriangles2D(const Vertex2DMTL* verts, int vertCount, const uint16_t* indices,
                                         int indexCount, int textureHandle, int clipX, int clipY, int clipW,
                                         int clipH)
{
    if (_impl->currentEncoder == nullptr || vertCount <= 0 || indexCount <= 0)
        return;

    // Clamp to the drawable -- Metal's setScissorRect raises a validation
    // error if the rect extends past the render target.
    int x0 = clipX < 0 ? 0 : clipX;
    int y0 = clipY < 0 ? 0 : clipY;
    int x1 = clipX + clipW;
    int y1 = clipY + clipH;
    if (x1 > _impl->drawableWidth)
        x1 = _impl->drawableWidth;
    if (y1 > _impl->drawableHeight)
        y1 = _impl->drawableHeight;
    if (x1 <= x0 || y1 <= y0)
        return; // fully clipped

    MTL::ScissorRect scissor;
    scissor.x = static_cast<NS::UInteger>(x0);
    scissor.y = static_cast<NS::UInteger>(y0);
    scissor.width = static_cast<NS::UInteger>(x1 - x0);
    scissor.height = static_cast<NS::UInteger>(y1 - y0);
    _impl->currentEncoder->setScissorRect(scissor);

    MTL::Buffer* vbuf = _impl->device->newBuffer(verts, static_cast<NS::UInteger>(vertCount) * sizeof(Vertex2DMTL),
                                                 MTL::ResourceStorageModeShared);
    MTL::Buffer* ibuf = _impl->device->newBuffer(indices, static_cast<NS::UInteger>(indexCount) * sizeof(uint16_t),
                                                 MTL::ResourceStorageModeShared);

    MTL::Texture* tex = _impl->fallbackWhite;
    if (textureHandle > 0 && static_cast<size_t>(textureHandle) <= _impl->textures.size())
    {
        MTL::Texture* found = _impl->textures[textureHandle - 1];
        if (found != nullptr)
            tex = found;
    }

    _impl->currentEncoder->setVertexBuffer(vbuf, 0, 0);
    _impl->currentEncoder->setFragmentTexture(tex, 0);
    _impl->currentEncoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, static_cast<NS::UInteger>(indexCount),
                                                 MTL::IndexTypeUInt16, ibuf, 0);

    vbuf->release();
    ibuf->release();
}

void EngineMTLBootstrap::EndFrame()
{
    if (_impl->currentEncoder == nullptr)
        return;

    // Local pool just for incidental temporaries -- see BeginFrame()'s
    // comment. The encoder/command buffer/drawable were explicitly
    // retain()'d when stored, so release them explicitly here too, after
    // they're done being used (endEncoding/presentDrawable/commit), rather
    // than relying on any pool's drain timing.
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    _impl->currentEncoder->endEncoding();
    _impl->currentCommandBuffer->presentDrawable(_impl->currentDrawable);
    _impl->currentCommandBuffer->commit();

    _impl->currentEncoder->release();
    _impl->currentCommandBuffer->release();
    _impl->currentDrawable->release();

    _impl->currentEncoder = nullptr;
    _impl->currentCommandBuffer = nullptr;
    _impl->currentDrawable = nullptr;

    pool->release();
}

int EngineMTLBootstrap::CreateTexture(int width, int height, const uint8_t* rgba)
{
    if (_impl->device == nullptr || width <= 0 || height <= 0 || rgba == nullptr)
        return 0;

    MTL::TextureDescriptor* desc =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, static_cast<NS::UInteger>(width),
                                                    static_cast<NS::UInteger>(height), false);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);

    MTL::Texture* tex = _impl->device->newTexture(desc);
    desc->release();
    if (tex == nullptr)
        return 0;

    MTL::Region region = MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height));
    tex->replaceRegion(region, 0, rgba, static_cast<NS::UInteger>(width) * 4);

    _impl->textures.push_back(tex);
    return static_cast<int>(_impl->textures.size());
}

void EngineMTLBootstrap::UpdateTexture(int handle, int width, int height, const uint8_t* rgba)
{
    if (handle <= 0 || static_cast<size_t>(handle) > _impl->textures.size() || rgba == nullptr)
        return;
    MTL::Texture* tex = _impl->textures[handle - 1];
    if (tex == nullptr)
        return;

    MTL::Region region = MTL::Region::Make2D(0, 0, static_cast<NS::UInteger>(width), static_cast<NS::UInteger>(height));
    tex->replaceRegion(region, 0, rgba, static_cast<NS::UInteger>(width) * 4);
}

void EngineMTLBootstrap::DestroyTexture(int handle)
{
    if (handle <= 0 || static_cast<size_t>(handle) > _impl->textures.size())
        return;
    MTL::Texture*& slot = _impl->textures[handle - 1];
    if (slot != nullptr)
    {
        slot->release();
        slot = nullptr;
    }
}

void EngineMTLBootstrap::Shutdown()
{
    for (MTL::Texture*& tex : _impl->textures)
    {
        if (tex != nullptr)
        {
            tex->release();
            tex = nullptr;
        }
    }
    _impl->textures.clear();
    if (_impl->fallbackWhite != nullptr)
    {
        _impl->fallbackWhite->release();
        _impl->fallbackWhite = nullptr;
    }
    if (_impl->samplerState != nullptr)
    {
        _impl->samplerState->release();
        _impl->samplerState = nullptr;
    }
    if (_impl->pipelineState != nullptr)
    {
        _impl->pipelineState->release();
        _impl->pipelineState = nullptr;
    }
    if (_impl->commandQueue != nullptr)
    {
        _impl->commandQueue->release();
        _impl->commandQueue = nullptr;
    }
    if (_impl->device != nullptr)
    {
        _impl->device->release();
        _impl->device = nullptr;
    }
    _impl->layer = nullptr; // owned by the SDL_MetalView, not us

    if (_impl->metalView != nullptr)
    {
        SDL_Metal_DestroyView(_impl->metalView);
        _impl->metalView = nullptr;
    }
    // Only destroy the window if Init() created it. AttachToWindow() callers
    // (EngineMTL) own their own window's lifecycle.
    if (_ownsWindow && _window != nullptr)
    {
        SDL_DestroyWindow(_window);
    }
    _window = nullptr;
}

} // namespace Poseidon
