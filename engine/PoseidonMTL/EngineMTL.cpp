#include <PoseidonMTL/EngineMTL.hpp>

// metal-cpp is header-only; the *_PRIVATE_IMPLEMENTATION macros must be
// defined in exactly one translation unit across the whole binary to emit
// the Objective-C bridging glue. This is that translation unit.
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <cstdio>

namespace Poseidon
{

struct EngineMTLBootstrap::Impl
{
    SDL_MetalView metalView = nullptr;
    CA::MetalLayer* layer = nullptr;
    MTL::Device* device = nullptr;
    MTL::CommandQueue* commandQueue = nullptr;
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

    _impl->commandQueue = _impl->device->newCommandQueue();
    if (_impl->commandQueue == nullptr)
    {
        std::fprintf(stderr, "EngineMTLBootstrap: newCommandQueue() returned null\n");
        return false;
    }

    return true;
}

void EngineMTLBootstrap::RenderClearAndPresent(float r, float g, float b, float a)
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
    colorAttachment->setLoadAction(MTL::LoadActionClear);
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
    _impl->layer->setDrawableSize(CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height)));
}

void EngineMTLBootstrap::Shutdown()
{
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
    if (_window != nullptr)
    {
        SDL_DestroyWindow(_window);
        _window = nullptr;
    }
}

} // namespace Poseidon
