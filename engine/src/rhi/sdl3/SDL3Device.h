#pragma once

#include "engine/rhi/IRHIDevice.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

namespace ds::rhi {

// ---------------------------------------------------------------------------
// SDL3 command list — wraps SDL_GPUCommandBuffer + active render pass
// ---------------------------------------------------------------------------
class SDL3CommandList final : public IRHICommandList {
  public:
    SDL3CommandList(SDL_GPUDevice* device, SDL_Window* window);

    // Called by SDL3Device::beginFrame / submitFrame
    bool acquire();
    void submit();

    SDL_GPUCommandBuffer* cmdBuf() const { return m_cmd; }
    SDL_GPUTexture* swapchainTex() const { return m_swapchainTex; }

    // IRHICommandList
    void beginRenderPass(const RenderPassDesc& desc) override;
    void endRenderPass() override;

    void setPipeline(RHIPipeline pipeline) override;
    void setViewport(float x, float y, float w, float h, float minDepth, float maxDepth) override;
    void setScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) override;

    void setVertexBuffer(uint32_t slot, RHIBuffer buffer, uint64_t offset) override;
    void setIndexBuffer(RHIBuffer buffer, IndexType type, uint64_t offset) override;

    void pushVertexConstants(const void* data, uint32_t size, uint32_t offset) override;
    void pushFragmentConstants(const void* data, uint32_t size, uint32_t offset) override;

    void bindVertexTexture(uint32_t slot, RHITexture texture, RHISampler sampler) override;
    void bindFragmentTexture(uint32_t slot, RHITexture texture, RHISampler sampler) override;

    void bindVertexUniform(uint32_t slot, RHIBuffer buffer, uint64_t offset, uint64_t size) override;
    void bindFragmentUniform(uint32_t slot, RHIBuffer buffer, uint64_t offset, uint64_t size) override;

    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;

    void uploadBuffer(RHIBuffer dst, const void* data, uint64_t size, uint64_t dstOffset) override;
    void uploadTexture(RHITexture dst, const void* data, uint64_t size, uint32_t mipLevel, uint32_t layer) override;
    void copyBuffer(RHIBuffer dst, uint64_t dstOffset, RHIBuffer src, uint64_t srcOffset, uint64_t size) override;

  private:
    SDL_GPUDevice* m_device         = nullptr;
    SDL_Window* m_window            = nullptr;
    SDL_GPUCommandBuffer* m_cmd     = nullptr;
    SDL_GPUTexture* m_swapchainTex  = nullptr;
    SDL_GPURenderPass* m_renderPass = nullptr;
};

// ---------------------------------------------------------------------------
// SDL3 device — owns the SDL_GPUDevice + window claim
// ---------------------------------------------------------------------------
class SDL3Device final : public IRHIDevice {
  public:
    SDL3Device(SDL_Window* window);
    ~SDL3Device() override;

    RHIBuffer createBuffer(const BufferDesc& desc) override;
    RHITexture createTexture(const TextureDesc& desc) override;
    RHISampler createSampler(const SamplerDesc& desc) override;
    RHIShader createShader(const ShaderDesc& desc) override;
    RHIPipeline createPipeline(const PipelineDesc& desc) override;

    void destroyBuffer(RHIBuffer buffer) override;
    void destroyTexture(RHITexture texture) override;
    void destroySampler(RHISampler sampler) override;
    void destroyShader(RHIShader shader) override;
    void destroyPipeline(RHIPipeline pipeline) override;

    IRHICommandList* beginFrame() override;
    void submitFrame(IRHICommandList* cmd) override;

    void uploadImmediate(RHIBuffer dst, const void* data, uint64_t size, uint64_t dstOffset) override;
    void uploadImmediateTexture(RHITexture dst, const void* data, uint64_t size,
                                uint32_t width, uint32_t height, uint32_t mipLevel) override;

    const RHICaps& caps() const override { return m_caps; }
    TextureFormat swapchainFormat() const override;

    void* nativeDevice() const override { return m_gpu; }

    // Debug: download an RGBA8/BGRA8 render-target texture to a PPM file.
    void debugDownloadTexture(RHITexture tex, uint32_t w, uint32_t h, const char* path);

  private:
    SDL_GPUTextureFormat toSDLFormat(TextureFormat fmt) const;
    SDL_GPUVertexElementFormat toSDLVertexFormat(const VertexAttribute& attr) const;

    SDL_GPUDevice* m_gpu        = nullptr;
    SDL_Window* m_window        = nullptr;
    RHICaps m_caps              = {};
    SDL3CommandList* m_frameCmd = nullptr;
};

} // namespace ds::rhi
