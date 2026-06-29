#include "SDL3Device.h"
#include <stdexcept>
#include <cstring>
#include <string>
#include <vector>

namespace ds::rhi {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void sdlCheck(bool ok, const char* msg) {
    if (!ok) throw std::runtime_error(std::string(msg) + ": " + SDL_GetError());
}

// ---------------------------------------------------------------------------
// SDL3Device
// ---------------------------------------------------------------------------
SDL3Device::SDL3Device(SDL_Window* window) : m_window(window) {
    m_gpu = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL,
        false, nullptr);
    sdlCheck(m_gpu != nullptr, "SDL_CreateGPUDevice");
    sdlCheck(SDL_ClaimWindowForGPUDevice(m_gpu, m_window), "SDL_ClaimWindowForGPUDevice");

    m_caps.maxTextureDim     = 16384;
    m_caps.maxPushConstBytes = 128;

    m_frameCmd = new SDL3CommandList(m_gpu, m_window);
}

SDL3Device::~SDL3Device() {
    delete m_frameCmd;
    if (m_gpu && m_window) SDL_ReleaseWindowFromGPUDevice(m_gpu, m_window);
    if (m_gpu)             SDL_DestroyGPUDevice(m_gpu);
}

// ---------------------------------------------------------------------------
// Buffer
// ---------------------------------------------------------------------------
RHIBuffer SDL3Device::createBuffer(const BufferDesc& desc) {
    SDL_GPUBufferUsageFlags usage = 0;
    if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::Vertex))
        usage |= SDL_GPU_BUFFERUSAGE_VERTEX;
    if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::Index))
        usage |= SDL_GPU_BUFFERUSAGE_INDEX;
    if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::Storage))
        usage |= SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;

    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size  = static_cast<uint32_t>(desc.size);

    SDL_GPUBuffer* buf = SDL_CreateGPUBuffer(m_gpu, &info);
    sdlCheck(buf != nullptr, "SDL_CreateGPUBuffer");
    if (desc.debugName) SDL_SetGPUBufferName(m_gpu, buf, desc.debugName);

    RHIBuffer handle;
    handle.ptr = buf;
    return handle;
}

void SDL3Device::destroyBuffer(RHIBuffer buffer) {
    if (buffer.valid())
        SDL_ReleaseGPUBuffer(m_gpu, static_cast<SDL_GPUBuffer*>(buffer.ptr));
}

// ---------------------------------------------------------------------------
// Texture
// ---------------------------------------------------------------------------
SDL_GPUTextureFormat SDL3Device::toSDLFormat(TextureFormat fmt) const {
    switch (fmt) {
        case TextureFormat::RGBA8Unorm:     return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        case TextureFormat::BGRA8Unorm:     return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        case TextureFormat::R8Unorm:        return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        case TextureFormat::RG8Unorm:       return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
        case TextureFormat::RGBA16Float:    return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        case TextureFormat::R32Float:       return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        case TextureFormat::D32Float:       return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        case TextureFormat::D24UnormS8Uint: return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::BC7Unorm:       return SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
        default:                            return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }
}

RHITexture SDL3Device::createTexture(const TextureDesc& desc) {
    SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (desc.isRenderTarget)  usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (desc.isDepthStencil)  usage |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;

    SDL_GPUTextureCreateInfo info{};
    info.type          = SDL_GPU_TEXTURETYPE_2D;
    info.format        = toSDLFormat(desc.format);
    info.usage         = usage;
    info.width         = desc.width;
    info.height        = desc.height;
    info.layer_count_or_depth = desc.depth;
    info.num_levels    = desc.mipLevels;

    SDL_GPUTexture* tex = SDL_CreateGPUTexture(m_gpu, &info);
    sdlCheck(tex != nullptr, "SDL_CreateGPUTexture");
    if (desc.debugName) SDL_SetGPUTextureName(m_gpu, tex, desc.debugName);

    RHITexture handle;
    handle.ptr = tex;
    return handle;
}

void SDL3Device::destroyTexture(RHITexture texture) {
    if (texture.valid())
        SDL_ReleaseGPUTexture(m_gpu, static_cast<SDL_GPUTexture*>(texture.ptr));
}

// ---------------------------------------------------------------------------
// Sampler
// ---------------------------------------------------------------------------
RHISampler SDL3Device::createSampler(const SamplerDesc& desc) {
    auto toFilter = [](FilterMode f) {
        return f == FilterMode::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    };
    auto toAddr = [](AddressMode a) -> SDL_GPUSamplerAddressMode {
        switch (a) {
            case AddressMode::Repeat:         return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            case AddressMode::MirroredRepeat: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
            case AddressMode::ClampToEdge:    return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            default:                          return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        }
    };

    SDL_GPUSamplerCreateInfo info{};
    info.min_filter     = toFilter(desc.minFilter);
    info.mag_filter     = toFilter(desc.magFilter);
    info.mipmap_mode    = desc.mipFilter == FilterMode::Linear
                          ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                          : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = toAddr(desc.addressU);
    info.address_mode_v = toAddr(desc.addressV);
    info.address_mode_w = toAddr(desc.addressW);
    info.mip_lod_bias   = desc.mipLodBias;
    info.max_anisotropy = desc.maxAnisotropy;
    info.enable_anisotropy = desc.maxAnisotropy > 1.0f;
    info.min_lod        = desc.minLod;
    info.max_lod        = desc.maxLod;

    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(m_gpu, &info);
    sdlCheck(sampler != nullptr, "SDL_CreateGPUSampler");

    RHISampler handle;
    handle.ptr = sampler;
    return handle;
}

void SDL3Device::destroySampler(RHISampler sampler) {
    if (sampler.valid())
        SDL_ReleaseGPUSampler(m_gpu, static_cast<SDL_GPUSampler*>(sampler.ptr));
}

// ---------------------------------------------------------------------------
// Shader
// ---------------------------------------------------------------------------
RHIShader SDL3Device::createShader(const ShaderDesc& desc) {
    SDL_GPUShaderFormat sdlFmt = SDL_GPU_SHADERFORMAT_INVALID;
    switch (desc.format) {
        case ShaderFormat::SPIRV: sdlFmt = SDL_GPU_SHADERFORMAT_SPIRV; break;
        case ShaderFormat::MSL:   sdlFmt = SDL_GPU_SHADERFORMAT_MSL;   break;
        case ShaderFormat::DXIL:  sdlFmt = SDL_GPU_SHADERFORMAT_DXIL;  break;
    }

    SDL_GPUShaderCreateInfo info{};
    info.code          = static_cast<const uint8_t*>(desc.bytecode);
    info.code_size     = desc.bytecodeSize;
    info.entrypoint    = desc.entryPoint;
    info.format        = sdlFmt;
    info.stage         = desc.stage == ShaderStage::Vertex
                         ? SDL_GPU_SHADERSTAGE_VERTEX
                         : SDL_GPU_SHADERSTAGE_FRAGMENT;
    // sampler/uniform counts set to sane defaults; update when pipeline is built
    info.num_samplers          = 8;
    info.num_uniform_buffers   = 4;
    info.num_storage_buffers   = 0;
    info.num_storage_textures  = 0;

    SDL_GPUShader* shader = SDL_CreateGPUShader(m_gpu, &info);
    sdlCheck(shader != nullptr, "SDL_CreateGPUShader");

    RHIShader handle;
    handle.ptr = shader;
    return handle;
}

void SDL3Device::destroyShader(RHIShader shader) {
    if (shader.valid())
        SDL_ReleaseGPUShader(m_gpu, static_cast<SDL_GPUShader*>(shader.ptr));
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------
SDL_GPUVertexElementFormat SDL3Device::toSDLVertexFormat(const VertexAttribute& attr) const {
    if (!attr.isFloat) return SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    switch (attr.elementCount) {
        case 1: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        case 2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        case 3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        case 4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        default: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    }
}

RHIPipeline SDL3Device::createPipeline(const PipelineDesc& desc) {
    // Vertex attributes
    std::vector<SDL_GPUVertexAttribute> attrs;
    for (const auto& a : desc.vertexAttributes) {
        SDL_GPUVertexAttribute sdlAttr{};
        sdlAttr.location    = a.location;
        sdlAttr.buffer_slot = a.binding;
        sdlAttr.offset      = a.offset;
        sdlAttr.format      = toSDLVertexFormat(a);
        attrs.push_back(sdlAttr);
    }

    // Vertex buffer descriptions (SDL3 name for bindings)
    std::vector<SDL_GPUVertexBufferDescription> bindings;
    for (const auto& b : desc.vertexBindings) {
        SDL_GPUVertexBufferDescription sdlBind{};
        sdlBind.slot       = b.binding;
        sdlBind.pitch      = b.stride;
        sdlBind.input_rate = b.instanced
                           ? SDL_GPU_VERTEXINPUTRATE_INSTANCE
                           : SDL_GPU_VERTEXINPUTRATE_VERTEX;
        sdlBind.instance_step_rate = 0;
        bindings.push_back(sdlBind);
    }

    // Color targets
    std::vector<SDL_GPUColorTargetDescription> colorDescs;
    for (const auto& c : desc.colorTargets) {
        SDL_GPUColorTargetDescription sdlColor{};
        sdlColor.format = toSDLFormat(c.format);
        if (c.blend.blendEnabled) {
            sdlColor.blend_state.enable_blend = true;
            // blend factors mapping omitted for brevity — extend as needed
        }
        colorDescs.push_back(sdlColor);
    }

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader   = static_cast<SDL_GPUShader*>(desc.vertexShader.ptr);
    info.fragment_shader = static_cast<SDL_GPUShader*>(desc.fragmentShader.ptr);

    info.vertex_input_state.vertex_attributes               = attrs.data();
    info.vertex_input_state.num_vertex_attributes           = static_cast<uint32_t>(attrs.size());
    info.vertex_input_state.vertex_buffer_descriptions      = bindings.data();
    info.vertex_input_state.num_vertex_buffers              = static_cast<uint32_t>(bindings.size());

    switch (desc.topology) {
        case PrimitiveTopology::TriangleList:
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; break;
        case PrimitiveTopology::TriangleStrip:
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP; break;
        case PrimitiveTopology::LineList:
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST; break;
    }

    switch (desc.cullMode) {
        case CullMode::None:  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;  break;
        case CullMode::Front: info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT; break;
        case CullMode::Back:  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;  break;
    }
    info.rasterizer_state.fill_mode = desc.fillMode == FillMode::Wireframe
                                      ? SDL_GPU_FILLMODE_LINE
                                      : SDL_GPU_FILLMODE_FILL;

    info.depth_stencil_state.enable_depth_test  = desc.depthTest;
    info.depth_stencil_state.enable_depth_write = desc.depthWrite;

    info.target_info.color_target_descriptions    = colorDescs.data();
    info.target_info.num_color_targets            = static_cast<uint32_t>(colorDescs.size());
    if (desc.hasDepth) {
        info.target_info.depth_stencil_format     = toSDLFormat(desc.depthFormat);
        info.target_info.has_depth_stencil_target = true;
    }

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(m_gpu, &info);
    sdlCheck(pipeline != nullptr, "SDL_CreateGPUGraphicsPipeline");

    RHIPipeline handle;
    handle.ptr = pipeline;
    return handle;
}

void SDL3Device::destroyPipeline(RHIPipeline pipeline) {
    if (pipeline.valid())
        SDL_ReleaseGPUGraphicsPipeline(m_gpu, static_cast<SDL_GPUGraphicsPipeline*>(pipeline.ptr));
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
IRHICommandList* SDL3Device::beginFrame() {
    if (!m_frameCmd->acquire()) return nullptr;
    return m_frameCmd;
}

void SDL3Device::submitFrame(IRHICommandList* cmd) {
    static_cast<SDL3CommandList*>(cmd)->submit();
}

// ---------------------------------------------------------------------------
// Immediate upload
// ---------------------------------------------------------------------------
void SDL3Device::uploadImmediate(RHIBuffer dst, const void* data,
                                  uint64_t size, uint64_t dstOffset) {
    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size  = static_cast<uint32_t>(size);

    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(m_gpu, &tbInfo);
    sdlCheck(tb != nullptr, "SDL_CreateGPUTransferBuffer");

    void* mapped = SDL_MapGPUTransferBuffer(m_gpu, tb, false);
    sdlCheck(mapped != nullptr, "SDL_MapGPUTransferBuffer");
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(m_gpu, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_gpu);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = tb;
    src.offset          = 0;

    SDL_GPUBufferRegion region{};
    region.buffer = static_cast<SDL_GPUBuffer*>(dst.ptr);
    region.offset = static_cast<uint32_t>(dstOffset);
    region.size   = static_cast<uint32_t>(size);

    SDL_UploadToGPUBuffer(copy, &src, &region, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(m_gpu, tb);
}

void SDL3Device::uploadImmediateTexture(RHITexture dst, const void* data,
                                         uint64_t size, uint32_t mipLevel) {
    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size  = static_cast<uint32_t>(size);

    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(m_gpu, &tbInfo);
    sdlCheck(tb != nullptr, "SDL_CreateGPUTransferBuffer (tex)");

    void* mapped = SDL_MapGPUTransferBuffer(m_gpu, tb, false);
    sdlCheck(mapped != nullptr, "SDL_MapGPUTransferBuffer (tex)");
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(m_gpu, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_gpu);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.offset          = 0;

    SDL_GPUTextureRegion region{};
    region.texture   = static_cast<SDL_GPUTexture*>(dst.ptr);
    region.mip_level = mipLevel;
    region.layer     = 0;

    SDL_UploadToGPUTexture(copy, &src, &region, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(m_gpu, tb);
}

// ---------------------------------------------------------------------------
// SDL3CommandList
// ---------------------------------------------------------------------------
SDL3CommandList::SDL3CommandList(SDL_GPUDevice* device, SDL_Window* window)
    : m_device(device), m_window(window) {}

bool SDL3CommandList::acquire() {
    m_cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (!m_cmd) return false;
    m_swapchainTex = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(m_cmd, m_window, &m_swapchainTex, nullptr, nullptr))
        return false;
    return true;
}

void SDL3CommandList::submit() {
    if (m_renderPass) {
        SDL_EndGPURenderPass(m_renderPass);
        m_renderPass = nullptr;
    }
    SDL_SubmitGPUCommandBuffer(m_cmd);
    m_cmd          = nullptr;
    m_swapchainTex = nullptr;
}

void SDL3CommandList::beginRenderPass(const RenderPassDesc& desc) {
    std::vector<SDL_GPUColorTargetInfo> colorInfos;
    for (const auto& att : desc.colorAttachments) {
        SDL_GPUColorTargetInfo ci{};
        ci.texture     = att.texture.valid()
                         ? static_cast<SDL_GPUTexture*>(att.texture.ptr)
                         : m_swapchainTex;
        ci.load_op     = att.loadOp  == LoadOp::Clear    ? SDL_GPU_LOADOP_CLEAR
                       : att.loadOp  == LoadOp::Load     ? SDL_GPU_LOADOP_LOAD
                                                         : SDL_GPU_LOADOP_DONT_CARE;
        ci.store_op    = att.storeOp == StoreOp::Store   ? SDL_GPU_STOREOP_STORE
                                                         : SDL_GPU_STOREOP_DONT_CARE;
        ci.clear_color = {att.clearColor[0], att.clearColor[1],
                          att.clearColor[2], att.clearColor[3]};
        colorInfos.push_back(ci);
    }

    SDL_GPUDepthStencilTargetInfo* depthPtr = nullptr;
    SDL_GPUDepthStencilTargetInfo depthInfo{};
    if (desc.depthAttachment) {
        const auto& da = *desc.depthAttachment;
        depthInfo.texture     = static_cast<SDL_GPUTexture*>(da.texture.ptr);
        depthInfo.load_op     = da.loadOp  == LoadOp::Clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        depthInfo.store_op    = da.storeOp == StoreOp::Store ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
        depthInfo.clear_depth = da.clearDepth;
        depthPtr = &depthInfo;
    }

    m_renderPass = SDL_BeginGPURenderPass(m_cmd,
                                          colorInfos.data(),
                                          static_cast<uint32_t>(colorInfos.size()),
                                          depthPtr);
}

void SDL3CommandList::endRenderPass() {
    if (m_renderPass) {
        SDL_EndGPURenderPass(m_renderPass);
        m_renderPass = nullptr;
    }
}

void SDL3CommandList::setPipeline(RHIPipeline pipeline) {
    SDL_BindGPUGraphicsPipeline(m_renderPass,
        static_cast<SDL_GPUGraphicsPipeline*>(pipeline.ptr));
}

void SDL3CommandList::setViewport(float x, float y, float w, float h,
                                   float minDepth, float maxDepth) {
    SDL_GPUViewport vp{ x, y, w, h, minDepth, maxDepth };
    SDL_SetGPUViewport(m_renderPass, &vp);
}

void SDL3CommandList::setScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    SDL_Rect r{ x, y, static_cast<int>(w), static_cast<int>(h) };
    SDL_SetGPUScissor(m_renderPass, &r);
}

void SDL3CommandList::setVertexBuffer(uint32_t slot, RHIBuffer buffer, uint64_t offset) {
    SDL_GPUBufferBinding binding{ static_cast<SDL_GPUBuffer*>(buffer.ptr),
                                  static_cast<uint32_t>(offset) };
    SDL_BindGPUVertexBuffers(m_renderPass, slot, &binding, 1);
}

void SDL3CommandList::setIndexBuffer(RHIBuffer buffer, IndexType type, uint64_t offset) {
    SDL_GPUBufferBinding binding{ static_cast<SDL_GPUBuffer*>(buffer.ptr),
                                  static_cast<uint32_t>(offset) };
    SDL_BindGPUIndexBuffer(m_renderPass, &binding,
        type == IndexType::Uint16 ? SDL_GPU_INDEXELEMENTSIZE_16BIT
                                  : SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

void SDL3CommandList::pushVertexConstants(const void* data, uint32_t size, uint32_t offset) {
    SDL_PushGPUVertexUniformData(m_cmd, offset, data, size);
}

void SDL3CommandList::pushFragmentConstants(const void* data, uint32_t size, uint32_t offset) {
    SDL_PushGPUFragmentUniformData(m_cmd, offset, data, size);
}

void SDL3CommandList::bindVertexTexture(uint32_t slot, RHITexture texture, RHISampler sampler) {
    SDL_GPUTextureSamplerBinding binding{
        static_cast<SDL_GPUTexture*>(texture.ptr),
        static_cast<SDL_GPUSampler*>(sampler.ptr)
    };
    SDL_BindGPUVertexSamplers(m_renderPass, slot, &binding, 1);
}

void SDL3CommandList::bindFragmentTexture(uint32_t slot, RHITexture texture, RHISampler sampler) {
    SDL_GPUTextureSamplerBinding binding{
        static_cast<SDL_GPUTexture*>(texture.ptr),
        static_cast<SDL_GPUSampler*>(sampler.ptr)
    };
    SDL_BindGPUFragmentSamplers(m_renderPass, slot, &binding, 1);
}

void SDL3CommandList::bindVertexUniform(uint32_t slot, RHIBuffer, uint64_t, uint64_t) {
    // SDL3 GPU uses push constants for small uniforms; large UBOs via storage buffers
    // Slot-based UBO binding is handled via SDL_PushGPUVertexUniformData
    (void)slot;
}

void SDL3CommandList::bindFragmentUniform(uint32_t slot, RHIBuffer, uint64_t, uint64_t) {
    (void)slot;
}

void SDL3CommandList::draw(uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) {
    SDL_DrawGPUPrimitives(m_renderPass, vertexCount, instanceCount, firstVertex, firstInstance);
}

void SDL3CommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t vertexOffset,
                                   uint32_t firstInstance) {
    SDL_DrawGPUIndexedPrimitives(m_renderPass, indexCount, instanceCount,
                                  firstIndex, vertexOffset, firstInstance);
}

void SDL3CommandList::uploadBuffer(RHIBuffer, const void*, uint64_t, uint64_t) {
    // In-frame uploads require a copy pass which must happen outside render pass.
    // Use IRHIDevice::uploadImmediate for now; in-frame staging queue is future work.
}

void SDL3CommandList::uploadTexture(RHITexture, const void*, uint64_t, uint32_t, uint32_t) {
    // Same as above — use uploadImmediateTexture.
}

void SDL3CommandList::copyBuffer(RHIBuffer dst, uint64_t dstOffset,
                                  RHIBuffer src, uint64_t srcOffset, uint64_t size) {
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(m_cmd);
    SDL_GPUBufferLocation srcLoc{ static_cast<SDL_GPUBuffer*>(src.ptr),
                                   static_cast<uint32_t>(srcOffset) };
    SDL_GPUBufferLocation dstLoc{ static_cast<SDL_GPUBuffer*>(dst.ptr),
                                   static_cast<uint32_t>(dstOffset) };
    SDL_CopyGPUBufferToBuffer(copy, &srcLoc, &dstLoc, static_cast<uint32_t>(size), false);
    SDL_EndGPUCopyPass(copy);
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
std::unique_ptr<IRHIDevice> createDevice(void* windowHandle) {
    return std::make_unique<SDL3Device>(static_cast<SDL_Window*>(windowHandle));
}

} // namespace ds::rhi
