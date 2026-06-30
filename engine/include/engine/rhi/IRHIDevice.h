#pragma once

#include <memory>

#include "IRHICommandList.h"
#include "RHITypes.h"

namespace ds::rhi {

class IRHIDevice {
  public:
    virtual ~IRHIDevice() = default;

    // -----------------------------------------------------------------------
    // Resource creation
    // -----------------------------------------------------------------------
    virtual RHIBuffer createBuffer(const BufferDesc& desc)       = 0;
    virtual RHITexture createTexture(const TextureDesc& desc)    = 0;
    virtual RHISampler createSampler(const SamplerDesc& desc)    = 0;
    virtual RHIShader createShader(const ShaderDesc& desc)       = 0;
    virtual RHIPipeline createPipeline(const PipelineDesc& desc) = 0;

    // Compute pipeline (task 39). Default-implemented to return an invalid
    // handle so backends without compute support (or stubs) still compile; the
    // SDL3 backend overrides it. A caller MUST check valid() before dispatching.
    virtual RHIComputePipeline createComputePipeline(const ComputePipelineDesc& desc) {
        (void)desc;
        return {};
    }

    // -----------------------------------------------------------------------
    // Resource destruction
    // -----------------------------------------------------------------------
    virtual void destroyBuffer(RHIBuffer buffer)       = 0;
    virtual void destroyTexture(RHITexture texture)    = 0;
    virtual void destroySampler(RHISampler sampler)    = 0;
    virtual void destroyShader(RHIShader shader)       = 0;
    virtual void destroyPipeline(RHIPipeline pipeline) = 0;
    virtual void destroyComputePipeline(RHIComputePipeline pipeline) { (void)pipeline; }

    // -----------------------------------------------------------------------
    // Frame lifecycle
    //   beginFrame  — acquires swapchain image, returns command list
    //   submitFrame — submits and presents; cmd is invalid after this call
    // -----------------------------------------------------------------------
    virtual IRHICommandList* beginFrame()          = 0;
    virtual void submitFrame(IRHICommandList* cmd) = 0;

    // -----------------------------------------------------------------------
    // Immediate upload helper (syncs on completion — use for init only)
    // -----------------------------------------------------------------------
    virtual void uploadImmediate(RHIBuffer dst, const void* data, uint64_t size, uint64_t dstOffset = 0) = 0;
    virtual void uploadImmediateTexture(RHITexture dst, const void* data, uint64_t size, uint32_t width,
                                        uint32_t height, uint32_t mipLevel = 0)                          = 0;

    // -----------------------------------------------------------------------
    // Capabilities
    // -----------------------------------------------------------------------
    virtual const RHICaps& caps() const           = 0;
    virtual TextureFormat swapchainFormat() const = 0;

    // -----------------------------------------------------------------------
    // Native handle escape hatch for extended features (mesh shaders, etc.)
    // Returns nullptr if backend doesn't expose native handles.
    // -----------------------------------------------------------------------
    virtual void* nativeDevice() const { return nullptr; }
    virtual void* nativeQueue() const { return nullptr; }
    virtual void* nativeInstance() const { return nullptr; }
};

// Factory — returns backend based on platform / caps at runtime
std::unique_ptr<IRHIDevice> createDevice(void* windowHandle);

} // namespace ds::rhi
