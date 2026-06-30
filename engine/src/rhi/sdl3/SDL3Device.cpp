#include "SDL3Device.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace ds::rhi {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void sdlCheck(bool ok, const char* msg) {
    if (!ok)
        throw std::runtime_error(std::string(msg) + ": " + SDL_GetError());
}

// ---------------------------------------------------------------------------
// SDL3Device
// ---------------------------------------------------------------------------
SDL3Device::SDL3Device(SDL_Window* window) : m_window(window) {
#if defined(DS_DEV)
    m_gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL, true,
                                nullptr); // debug_mode=true catches validation errors
#else
    m_gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL,
                                false, nullptr);
#endif
    sdlCheck(m_gpu != nullptr, "SDL_CreateGPUDevice");
    sdlCheck(SDL_ClaimWindowForGPUDevice(m_gpu, m_window), "SDL_ClaimWindowForGPUDevice");

    m_caps.maxTextureDim     = 16384;
    m_caps.maxPushConstBytes = 128;

    // Best-effort capability population (task 34). SDL3 GPU deliberately exposes
    // a small, portable surface: it has NO query for mesh shaders, bindless, or
    // device VRAM. So we derive the two feature flags QualityProfile::selectTier
    // reads (meshShaders / bindless) from the shader format the driver chose, and
    // leave deviceVRAMBytes at 0 ("unknown"). selectTier treats 0 VRAM
    // conservatively as Minimum, so an unknown VRAM never over-promises.
    //
    // Heuristic: a DXIL-capable backend is the modern D3D12 path, and an MSL
    // backend is Metal on Apple Silicon / recent Macs — both ship on hardware
    // that comfortably clears the "Enhanced" bar (mesh shaders + bindless-style
    // descriptor indexing) in practice. The Vulkan/SPIR-V path covers everything
    // from a GTX 1060 up, so we cannot assume Enhanced from SPIR-V alone and
    // leave the flags false there (Minimum unless a future VRAM query says
    // otherwise). This is intentionally conservative; correctness of the render
    // path does not depend on the tier, only its cost.
    const SDL_GPUShaderFormat fmts = SDL_GetGPUShaderFormats(m_gpu);
    if ((fmts & SDL_GPU_SHADERFORMAT_DXIL) != 0u || (fmts & SDL_GPU_SHADERFORMAT_MSL) != 0u) {
        m_caps.meshShaders = true;
        m_caps.bindless    = true;
    }
    m_caps.deviceVRAMBytes = 0; // SDL3 GPU exposes no VRAM query; 0 == unknown.

    m_frameCmd = new SDL3CommandList(m_gpu, m_window);
}

SDL3Device::~SDL3Device() {
    delete m_frameCmd;
    if (m_gpu && m_window)
        SDL_ReleaseWindowFromGPUDevice(m_gpu, m_window);
    if (m_gpu)
        SDL_DestroyGPUDevice(m_gpu);
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
    if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::StorageWrite))
        usage |= SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;

    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size  = static_cast<uint32_t>(desc.size);

    SDL_GPUBuffer* buf = SDL_CreateGPUBuffer(m_gpu, &info);
    sdlCheck(buf != nullptr, "SDL_CreateGPUBuffer");
    if (desc.debugName)
        SDL_SetGPUBufferName(m_gpu, buf, desc.debugName);

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
    case TextureFormat::RGBA8Unorm:
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case TextureFormat::BGRA8Unorm:
        return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case TextureFormat::R8Unorm:
        return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    case TextureFormat::RG8Unorm:
        return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    case TextureFormat::RGBA16Float:
        return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::R32Float:
        return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    case TextureFormat::D32Float:
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    case TextureFormat::D24UnormS8Uint:
        return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    case TextureFormat::BC7Unorm:
        return SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
    default:
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }
}

RHITexture SDL3Device::createTexture(const TextureDesc& desc) {
    SDL_GPUTextureUsageFlags usage = 0;
    if (desc.isDepthStencil) {
        usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        // A depth texture flagged as a render target additionally wants the
        // SAMPLER bit so it can be read in a later pass (e.g. the shadow map
        // sampled by the mesh shader).
        if (desc.isRenderTarget)
            usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    } else {
        usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        if (desc.isRenderTarget)
            usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type                 = SDL_GPU_TEXTURETYPE_2D;
    info.format               = toSDLFormat(desc.format);
    info.usage                = usage;
    info.width                = desc.width;
    info.height               = desc.height;
    info.layer_count_or_depth = desc.depth;
    info.num_levels           = desc.mipLevels;

    SDL_GPUTexture* tex = SDL_CreateGPUTexture(m_gpu, &info);
    sdlCheck(tex != nullptr, "SDL_CreateGPUTexture");
    if (desc.debugName)
        SDL_SetGPUTextureName(m_gpu, tex, desc.debugName);

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
        case AddressMode::Repeat:
            return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        case AddressMode::MirroredRepeat:
            return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:
            return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        default:
            return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        }
    };

    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = toFilter(desc.minFilter);
    info.mag_filter = toFilter(desc.magFilter);
    info.mipmap_mode =
        desc.mipFilter == FilterMode::Linear ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u    = toAddr(desc.addressU);
    info.address_mode_v    = toAddr(desc.addressV);
    info.address_mode_w    = toAddr(desc.addressW);
    info.mip_lod_bias      = desc.mipLodBias;
    info.max_anisotropy    = desc.maxAnisotropy;
    info.enable_anisotropy = desc.maxAnisotropy > 1.0f;
    info.min_lod           = desc.minLod;
    info.max_lod           = desc.maxLod;

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
    case ShaderFormat::SPIRV:
        sdlFmt = SDL_GPU_SHADERFORMAT_SPIRV;
        break;
    case ShaderFormat::MSL:
        sdlFmt = SDL_GPU_SHADERFORMAT_MSL;
        break;
    case ShaderFormat::DXIL:
        sdlFmt = SDL_GPU_SHADERFORMAT_DXIL;
        break;
    }

    SDL_GPUShaderCreateInfo info{};
    info.code         = static_cast<const uint8_t*>(desc.bytecode);
    info.code_size    = desc.bytecodeSize;
    info.entrypoint   = desc.entryPoint;
    info.format       = sdlFmt;
    info.stage        = desc.stage == ShaderStage::Vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = desc.numSamplers;
    info.num_uniform_buffers  = desc.numUniformBuffers;
    info.num_storage_buffers  = desc.numStorageBuffers;
    info.num_storage_textures = desc.numStorageTextures;

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
static SDL_GPUBlendFactor toSDLBlendFactor(BlendFactor f) {
    switch (f) {
    case BlendFactor::Zero:             return SDL_GPU_BLENDFACTOR_ZERO;
    case BlendFactor::One:              return SDL_GPU_BLENDFACTOR_ONE;
    case BlendFactor::SrcColor:         return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha:         return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha:         return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    default:                            return SDL_GPU_BLENDFACTOR_ONE;
    }
}

static SDL_GPUBlendOp toSDLBlendOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add:             return SDL_GPU_BLENDOP_ADD;
    case BlendOp::Subtract:        return SDL_GPU_BLENDOP_SUBTRACT;
    case BlendOp::ReverseSubtract: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
    case BlendOp::Min:             return SDL_GPU_BLENDOP_MIN;
    case BlendOp::Max:             return SDL_GPU_BLENDOP_MAX;
    default:                       return SDL_GPU_BLENDOP_ADD;
    }
}

static SDL_GPUCompareOp toSDLCompareOp(CompareOp op) {
    switch (op) {
    case CompareOp::Never:
        return SDL_GPU_COMPAREOP_NEVER;
    case CompareOp::Less:
        return SDL_GPU_COMPAREOP_LESS;
    case CompareOp::Equal:
        return SDL_GPU_COMPAREOP_EQUAL;
    case CompareOp::LessEqual:
        return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case CompareOp::Greater:
        return SDL_GPU_COMPAREOP_GREATER;
    case CompareOp::NotEqual:
        return SDL_GPU_COMPAREOP_NOT_EQUAL;
    case CompareOp::GreaterEqual:
        return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    case CompareOp::Always:
        return SDL_GPU_COMPAREOP_ALWAYS;
    default:
        return SDL_GPU_COMPAREOP_LESS;
    }
}

SDL_GPUVertexElementFormat SDL3Device::toSDLVertexFormat(const VertexAttribute& attr) const {
    if (!attr.isFloat)
        return SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    switch (attr.elementCount) {
    case 1:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    case 2:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case 3:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case 4:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    default:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
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
        sdlBind.slot               = b.binding;
        sdlBind.pitch              = b.stride;
        sdlBind.input_rate         = b.instanced ? SDL_GPU_VERTEXINPUTRATE_INSTANCE : SDL_GPU_VERTEXINPUTRATE_VERTEX;
        sdlBind.instance_step_rate = 0;
        bindings.push_back(sdlBind);
    }

    // Color targets
    std::vector<SDL_GPUColorTargetDescription> colorDescs;
    for (const auto& c : desc.colorTargets) {
        SDL_GPUColorTargetDescription sdlColor{};
        sdlColor.format = toSDLFormat(c.format);
        if (c.blend.blendEnabled) {
            sdlColor.blend_state.enable_blend          = true;
            sdlColor.blend_state.color_blend_op        = toSDLBlendOp(c.blend.colorOp);
            sdlColor.blend_state.alpha_blend_op        = toSDLBlendOp(c.blend.alphaOp);
            sdlColor.blend_state.src_color_blendfactor = toSDLBlendFactor(c.blend.srcColor);
            sdlColor.blend_state.dst_color_blendfactor = toSDLBlendFactor(c.blend.dstColor);
            sdlColor.blend_state.src_alpha_blendfactor = toSDLBlendFactor(c.blend.srcAlpha);
            sdlColor.blend_state.dst_alpha_blendfactor = toSDLBlendFactor(c.blend.dstAlpha);
        }
        colorDescs.push_back(sdlColor);
    }

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader   = static_cast<SDL_GPUShader*>(desc.vertexShader.ptr);
    info.fragment_shader = static_cast<SDL_GPUShader*>(desc.fragmentShader.ptr);

    info.vertex_input_state.vertex_attributes          = attrs.data();
    info.vertex_input_state.num_vertex_attributes      = static_cast<uint32_t>(attrs.size());
    info.vertex_input_state.vertex_buffer_descriptions = bindings.data();
    info.vertex_input_state.num_vertex_buffers         = static_cast<uint32_t>(bindings.size());

    switch (desc.topology) {
    case PrimitiveTopology::TriangleList:
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        break;
    case PrimitiveTopology::TriangleStrip:
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        break;
    case PrimitiveTopology::LineList:
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
        break;
    }

    switch (desc.cullMode) {
    case CullMode::None:
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        break;
    case CullMode::Front:
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT;
        break;
    case CullMode::Back:
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        break;
    }
    info.rasterizer_state.fill_mode =
        desc.fillMode == FillMode::Wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;

    info.depth_stencil_state.enable_depth_test  = desc.depthTest;
    info.depth_stencil_state.enable_depth_write = desc.depthWrite;
    info.depth_stencil_state.compare_op         = toSDLCompareOp(desc.depthCompare);

    info.target_info.color_target_descriptions = colorDescs.data();
    info.target_info.num_color_targets         = static_cast<uint32_t>(colorDescs.size());
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

void SDL3Device::setVSync(bool enabled) {
    SDL_GPUPresentMode mode = enabled ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
    if (!SDL_WindowSupportsGPUPresentMode(m_gpu, m_window, mode))
        return; // mode not supported; leave current setting unchanged
    SDL_SetGPUSwapchainParameters(m_gpu, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
}

TextureFormat SDL3Device::swapchainFormat() const {
    SDL_GPUTextureFormat fmt = SDL_GetGPUSwapchainTextureFormat(m_gpu, m_window);
    switch (fmt) {
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
        return TextureFormat::BGRA8Unorm;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return TextureFormat::RGBA8Unorm;
    default:
        return TextureFormat::BGRA8Unorm;
    }
}

void SDL3Device::destroyPipeline(RHIPipeline pipeline) {
    if (pipeline.valid())
        SDL_ReleaseGPUGraphicsPipeline(m_gpu, static_cast<SDL_GPUGraphicsPipeline*>(pipeline.ptr));
}

// ---------------------------------------------------------------------------
// Compute pipeline (task 39)
// ---------------------------------------------------------------------------
RHIComputePipeline SDL3Device::createComputePipeline(const ComputePipelineDesc& desc) {
    SDL_GPUShaderFormat sdlFmt = SDL_GPU_SHADERFORMAT_SPIRV;
    switch (desc.format) {
    case ShaderFormat::SPIRV:
        sdlFmt = SDL_GPU_SHADERFORMAT_SPIRV;
        break;
    case ShaderFormat::MSL:
        sdlFmt = SDL_GPU_SHADERFORMAT_MSL;
        break;
    case ShaderFormat::DXIL:
        sdlFmt = SDL_GPU_SHADERFORMAT_DXIL;
        break;
    }

    SDL_GPUComputePipelineCreateInfo info{};
    info.code                          = static_cast<const uint8_t*>(desc.bytecode);
    info.code_size                     = desc.bytecodeSize;
    info.entrypoint                    = desc.entryPoint;
    info.format                        = sdlFmt;
    info.num_readonly_storage_buffers  = desc.numReadOnlyStorageBufs;
    info.num_readwrite_storage_buffers = desc.numReadWriteStorageBufs;
    info.num_uniform_buffers           = desc.numUniformBuffers;
    info.threadcount_x                 = desc.threadCountX;
    info.threadcount_y                 = desc.threadCountY;
    info.threadcount_z                 = desc.threadCountZ;

    SDL_GPUComputePipeline* pipeline = SDL_CreateGPUComputePipeline(m_gpu, &info);
    // Non-fatal: a backend without compute (or a missing shader) leaves the
    // handle invalid; the caller checks valid() and keeps the CPU fallback.
    if (!pipeline) {
        SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "SDL_CreateGPUComputePipeline failed: %s", SDL_GetError());
        return {};
    }
    (void)desc.debugName; // SDL3 GPU exposes no compute-pipeline naming entry point.

    RHIComputePipeline handle;
    handle.ptr = pipeline;
    return handle;
}

void SDL3Device::destroyComputePipeline(RHIComputePipeline pipeline) {
    if (pipeline.valid())
        SDL_ReleaseGPUComputePipeline(m_gpu, static_cast<SDL_GPUComputePipeline*>(pipeline.ptr));
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
IRHICommandList* SDL3Device::beginFrame() {
    if (!m_frameCmd->acquire())
        return nullptr;
    return m_frameCmd;
}

void SDL3Device::submitFrame(IRHICommandList* cmd) {
    static_cast<SDL3CommandList*>(cmd)->submit();
}

// ---------------------------------------------------------------------------
// Immediate upload
// ---------------------------------------------------------------------------
void SDL3Device::uploadImmediate(RHIBuffer dst, const void* data, uint64_t size, uint64_t dstOffset) {
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
    SDL_GPUCopyPass* copy     = SDL_BeginGPUCopyPass(cmd);

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

void SDL3Device::uploadImmediateTexture(RHITexture dst, const void* data, uint64_t size, uint32_t width,
                                        uint32_t height, uint32_t mipLevel) {
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
    SDL_GPUCopyPass* copy     = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    src.offset          = 0;
    src.pixels_per_row  = width;
    src.rows_per_layer  = height;

    SDL_GPUTextureRegion region{};
    region.texture   = static_cast<SDL_GPUTexture*>(dst.ptr);
    region.mip_level = mipLevel;
    region.layer     = 0;
    region.w         = width;
    region.h         = height;
    region.d         = 1;

    SDL_UploadToGPUTexture(copy, &src, &region, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(m_gpu, tb);
}

void SDL3Device::debugDownloadTexture(RHITexture tex, uint32_t w, uint32_t h, const char* path) {
    const uint32_t bytes = w * h * 4;
    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage              = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tbInfo.size               = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(m_gpu, &tbInfo);
    sdlCheck(tb != nullptr, "debug CreateGPUTransferBuffer");

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_gpu);
    SDL_GPUCopyPass* copy     = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region{};
    region.texture = static_cast<SDL_GPUTexture*>(tex.ptr);
    region.w       = w;
    region.h       = h;
    region.d       = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    dst.pixels_per_row  = w;
    dst.rows_per_layer  = h;
    SDL_DownloadFromGPUTexture(copy, &region, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(m_gpu, true, &fence, 1);
    SDL_ReleaseGPUFence(m_gpu, fence);

    void* mapped = SDL_MapGPUTransferBuffer(m_gpu, tb, false);
    sdlCheck(mapped != nullptr, "debug MapGPUTransferBuffer");
    const uint8_t* px = static_cast<const uint8_t*>(mapped);

    bool isBGRA      = (SDL_GetGPUSwapchainTextureFormat(m_gpu, m_window) == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    if (io) {
        char header[64];
        int hn = SDL_snprintf(header, sizeof(header), "P6\n%u %u\n255\n", w, h);
        SDL_WriteIO(io, header, hn);
        std::vector<uint8_t> rgb(w * h * 3);
        for (uint32_t i = 0; i < w * h; ++i) {
            uint8_t r = px[i * 4 + 0], g = px[i * 4 + 1], b = px[i * 4 + 2];
            if (isBGRA) {
                uint8_t t = r;
                r         = b;
                b         = t;
            }
            rgb[i * 3 + 0] = r;
            rgb[i * 3 + 1] = g;
            rgb[i * 3 + 2] = b;
        }
        SDL_WriteIO(io, rgb.data(), rgb.size());
        SDL_CloseIO(io);
        SDL_Log("[capture] wrote %s (%ux%u, %s)", path, w, h, isBGRA ? "BGRA" : "RGBA");
    }
    SDL_UnmapGPUTransferBuffer(m_gpu, tb);
    SDL_ReleaseGPUTransferBuffer(m_gpu, tb);
}

// ---------------------------------------------------------------------------
// SDL3CommandList
// ---------------------------------------------------------------------------
SDL3CommandList::SDL3CommandList(SDL_GPUDevice* device, SDL_Window* window) : m_device(device), m_window(window) {}

bool SDL3CommandList::acquire() {
    m_cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (!m_cmd)
        return false;
    m_swapchainTex = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(m_cmd, m_window, &m_swapchainTex, nullptr, nullptr)) {
        SDL_CancelGPUCommandBuffer(m_cmd);
        m_cmd = nullptr;
        return false;
    }
    if (!m_swapchainTex) {
        // Swapchain unavailable this frame (window resize / occlusion); submit empty cmd.
        SDL_SubmitGPUCommandBuffer(m_cmd);
        m_cmd = nullptr;
        return false;
    }
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
        ci.texture     = att.texture.valid() ? static_cast<SDL_GPUTexture*>(att.texture.ptr) : m_swapchainTex;
        ci.load_op     = att.loadOp == LoadOp::Clear  ? SDL_GPU_LOADOP_CLEAR
                         : att.loadOp == LoadOp::Load ? SDL_GPU_LOADOP_LOAD
                                                      : SDL_GPU_LOADOP_DONT_CARE;
        ci.store_op    = att.storeOp == StoreOp::Store ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
        ci.clear_color = {att.clearColor[0], att.clearColor[1], att.clearColor[2], att.clearColor[3]};
        colorInfos.push_back(ci);
    }

    SDL_GPUDepthStencilTargetInfo* depthPtr = nullptr;
    SDL_GPUDepthStencilTargetInfo depthInfo{};
    if (desc.depthAttachment) {
        const auto& da        = *desc.depthAttachment;
        depthInfo.texture     = static_cast<SDL_GPUTexture*>(da.texture.ptr);
        depthInfo.load_op     = da.loadOp == LoadOp::Clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        depthInfo.store_op    = da.storeOp == StoreOp::Store ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
        depthInfo.clear_depth = da.clearDepth;
        depthPtr              = &depthInfo;
    }

    m_renderPass = SDL_BeginGPURenderPass(m_cmd, colorInfos.data(), static_cast<uint32_t>(colorInfos.size()), depthPtr);
}

void SDL3CommandList::endRenderPass() {
    if (m_renderPass) {
        SDL_EndGPURenderPass(m_renderPass);
        m_renderPass = nullptr;
    }
}

void SDL3CommandList::setPipeline(RHIPipeline pipeline) {
    SDL_BindGPUGraphicsPipeline(m_renderPass, static_cast<SDL_GPUGraphicsPipeline*>(pipeline.ptr));
}

void SDL3CommandList::setViewport(float x, float y, float w, float h, float minDepth, float maxDepth) {
    SDL_GPUViewport vp{x, y, w, h, minDepth, maxDepth};
    SDL_SetGPUViewport(m_renderPass, &vp);
}

void SDL3CommandList::setScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    SDL_Rect r{x, y, static_cast<int>(w), static_cast<int>(h)};
    SDL_SetGPUScissor(m_renderPass, &r);
}

void SDL3CommandList::setVertexBuffer(uint32_t slot, RHIBuffer buffer, uint64_t offset) {
    SDL_GPUBufferBinding binding{static_cast<SDL_GPUBuffer*>(buffer.ptr), static_cast<uint32_t>(offset)};
    SDL_BindGPUVertexBuffers(m_renderPass, slot, &binding, 1);
}

void SDL3CommandList::setIndexBuffer(RHIBuffer buffer, IndexType type, uint64_t offset) {
    SDL_GPUBufferBinding binding{static_cast<SDL_GPUBuffer*>(buffer.ptr), static_cast<uint32_t>(offset)};
    SDL_BindGPUIndexBuffer(m_renderPass, &binding,
                           type == IndexType::Uint16 ? SDL_GPU_INDEXELEMENTSIZE_16BIT : SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

void SDL3CommandList::pushVertexConstants(const void* data, uint32_t size, uint32_t offset) {
    SDL_PushGPUVertexUniformData(m_cmd, offset, data, size);
}

void SDL3CommandList::pushFragmentConstants(const void* data, uint32_t size, uint32_t offset) {
    SDL_PushGPUFragmentUniformData(m_cmd, offset, data, size);
}

void SDL3CommandList::bindVertexTexture(uint32_t slot, RHITexture texture, RHISampler sampler) {
    SDL_GPUTextureSamplerBinding binding{static_cast<SDL_GPUTexture*>(texture.ptr),
                                         static_cast<SDL_GPUSampler*>(sampler.ptr)};
    SDL_BindGPUVertexSamplers(m_renderPass, slot, &binding, 1);
}

void SDL3CommandList::bindFragmentTexture(uint32_t slot, RHITexture texture, RHISampler sampler) {
    SDL_GPUTextureSamplerBinding binding{static_cast<SDL_GPUTexture*>(texture.ptr),
                                         static_cast<SDL_GPUSampler*>(sampler.ptr)};
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

void SDL3CommandList::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    SDL_DrawGPUPrimitives(m_renderPass, vertexCount, instanceCount, firstVertex, firstInstance);
}

void SDL3CommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                  int32_t vertexOffset, uint32_t firstInstance) {
    SDL_DrawGPUIndexedPrimitives(m_renderPass, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void SDL3CommandList::uploadBuffer(RHIBuffer, const void*, uint64_t, uint64_t) {
    // In-frame uploads require a copy pass which must happen outside render pass.
    // Use IRHIDevice::uploadImmediate for now; in-frame staging queue is future work.
}

void SDL3CommandList::uploadTexture(RHITexture, const void*, uint64_t, uint32_t, uint32_t) {
    // Same as above — use uploadImmediateTexture.
}

void SDL3CommandList::dispatchCompute(RHIComputePipeline pipeline, RHIBuffer readWrite, RHIBuffer readOnly,
                                      const void* uniformData, uint32_t uniformSize, uint32_t gx, uint32_t gy,
                                      uint32_t gz) {
    if (!pipeline.valid() || !readWrite.valid() || !m_cmd)
        return;

    // The read-write storage buffer is bound through BeginGPUComputePass; any
    // read-only storage buffers are bound afterwards on the pass.
    SDL_GPUStorageBufferReadWriteBinding rwBinding{};
    rwBinding.buffer = static_cast<SDL_GPUBuffer*>(readWrite.ptr);
    rwBinding.cycle  = false;

    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(m_cmd, nullptr, 0, &rwBinding, 1);
    if (!pass)
        return;

    SDL_BindGPUComputePipeline(pass, static_cast<SDL_GPUComputePipeline*>(pipeline.ptr));

    if (readOnly.valid()) {
        SDL_GPUBuffer* roBuf = static_cast<SDL_GPUBuffer*>(readOnly.ptr);
        SDL_BindGPUComputeStorageBuffers(pass, 0, &roBuf, 1);
    }

    // Compute uniform slot 0 (matches the shader's cbuffer binding).
    if (uniformData && uniformSize > 0)
        SDL_PushGPUComputeUniformData(m_cmd, 0, uniformData, uniformSize);

    SDL_DispatchGPUCompute(pass, gx, gy, gz);
    SDL_EndGPUComputePass(pass);
}

void SDL3CommandList::copyBuffer(RHIBuffer dst, uint64_t dstOffset, RHIBuffer src, uint64_t srcOffset, uint64_t size) {
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(m_cmd);
    SDL_GPUBufferLocation srcLoc{static_cast<SDL_GPUBuffer*>(src.ptr), static_cast<uint32_t>(srcOffset)};
    SDL_GPUBufferLocation dstLoc{static_cast<SDL_GPUBuffer*>(dst.ptr), static_cast<uint32_t>(dstOffset)};
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
