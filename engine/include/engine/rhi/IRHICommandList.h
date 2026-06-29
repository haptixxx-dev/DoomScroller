#pragma once

#include "RHITypes.h"

namespace ds::rhi {

class IRHICommandList {
  public:
    virtual ~IRHICommandList() = default;

    // -----------------------------------------------------------------------
    // Render pass
    // -----------------------------------------------------------------------
    virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void endRenderPass()                             = 0;

    // -----------------------------------------------------------------------
    // Pipeline + state
    // -----------------------------------------------------------------------
    virtual void setPipeline(RHIPipeline pipeline)                                                             = 0;
    virtual void setViewport(float x, float y, float w, float h, float minDepth = 0.0f, float maxDepth = 1.0f) = 0;
    virtual void setScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)                                      = 0;

    // -----------------------------------------------------------------------
    // Resource binding
    // -----------------------------------------------------------------------
    virtual void setVertexBuffer(uint32_t slot, RHIBuffer buffer, uint64_t offset = 0) = 0;
    virtual void setIndexBuffer(RHIBuffer buffer, IndexType type, uint64_t offset = 0) = 0;

    // Small per-draw data (maps to push constants / setVertexBytes)
    virtual void pushVertexConstants(const void* data, uint32_t size, uint32_t offset = 0)   = 0;
    virtual void pushFragmentConstants(const void* data, uint32_t size, uint32_t offset = 0) = 0;

    // Slot-based texture + sampler binding
    virtual void bindVertexTexture(uint32_t slot, RHITexture texture, RHISampler sampler)   = 0;
    virtual void bindFragmentTexture(uint32_t slot, RHITexture texture, RHISampler sampler) = 0;

    // Uniform buffer binding
    virtual void bindVertexUniform(uint32_t slot, RHIBuffer buffer, uint64_t offset = 0, uint64_t size = 0)   = 0;
    virtual void bindFragmentUniform(uint32_t slot, RHIBuffer buffer, uint64_t offset = 0, uint64_t size = 0) = 0;

    // -----------------------------------------------------------------------
    // Draw calls
    // -----------------------------------------------------------------------
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
                      uint32_t firstInstance = 0)                                  = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
                             int32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;

    // -----------------------------------------------------------------------
    // Data upload (outside render pass — copy pass in SDL3/Vulkan terms)
    // -----------------------------------------------------------------------
    virtual void uploadBuffer(RHIBuffer dst, const void* data, uint64_t size, uint64_t dstOffset = 0)            = 0;
    virtual void uploadTexture(RHITexture dst, const void* data, uint64_t size, uint32_t mipLevel = 0,
                               uint32_t layer = 0)                                                               = 0;
    virtual void copyBuffer(RHIBuffer dst, uint64_t dstOffset, RHIBuffer src, uint64_t srcOffset, uint64_t size) = 0;
};

} // namespace ds::rhi
