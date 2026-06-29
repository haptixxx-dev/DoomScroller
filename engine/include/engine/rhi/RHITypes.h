#pragma once

#include <cstdint>
#include <cstddef>
#include <span>

namespace ds::rhi {

// ---------------------------------------------------------------------------
// Opaque handles — backends store actual GPU objects behind these
// ---------------------------------------------------------------------------
template<typename Tag>
struct Handle {
    void* ptr = nullptr;
    bool valid() const { return ptr != nullptr; }
};

struct BufferTag   {};
struct TextureTag  {};
struct SamplerTag  {};
struct ShaderTag   {};
struct PipelineTag {};

using RHIBuffer   = Handle<BufferTag>;
using RHITexture  = Handle<TextureTag>;
using RHISampler  = Handle<SamplerTag>;
using RHIShader   = Handle<ShaderTag>;
using RHIPipeline = Handle<PipelineTag>;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
enum class BufferUsage : uint32_t {
    Vertex  = 1 << 0,
    Index   = 1 << 1,
    Uniform = 1 << 2,
    Storage = 1 << 3,
};
inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

enum class TextureFormat {
    RGBA8Unorm,
    BGRA8Unorm,
    R8Unorm,
    RG8Unorm,
    RGBA16Float,
    R32Float,
    D32Float,
    D24UnormS8Uint,
    BC7Unorm,
};

enum class ShaderStage  { Vertex, Fragment, Compute };
enum class ShaderFormat { SPIRV, MSL, DXIL };

enum class PrimitiveTopology { TriangleList, TriangleStrip, LineList };
enum class IndexType          { Uint16, Uint32 };
enum class CullMode           { None, Front, Back };
enum class FillMode           { Solid, Wireframe };
enum class CompareOp {
    Never, Less, Equal, LessEqual,
    Greater, NotEqual, GreaterEqual, Always
};
enum class BlendFactor {
    Zero, One,
    SrcColor, OneMinusSrcColor,
    SrcAlpha, OneMinusSrcAlpha,
    DstAlpha, OneMinusDstAlpha,
};
enum class BlendOp { Add, Subtract, ReverseSubtract, Min, Max };

enum class FilterMode  { Nearest, Linear };
enum class AddressMode { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder };

enum class LoadOp  { Load, Clear, DontCare };
enum class StoreOp { Store, DontCare };

// ---------------------------------------------------------------------------
// Descriptors — plain data, no virtuals
// ---------------------------------------------------------------------------
struct BufferDesc {
    uint64_t    size      = 0;
    BufferUsage usage     = BufferUsage::Vertex;
    bool        cpuVisible = false; // upload / readback
    const char* debugName  = nullptr;
};

struct TextureDesc {
    uint32_t      width     = 1;
    uint32_t      height    = 1;
    uint32_t      depth     = 1;
    uint32_t      mipLevels = 1;
    TextureFormat format    = TextureFormat::RGBA8Unorm;
    bool          isRenderTarget  = false;
    bool          isDepthStencil  = false;
    const char*   debugName       = nullptr;
};

struct SamplerDesc {
    FilterMode  minFilter  = FilterMode::Linear;
    FilterMode  magFilter  = FilterMode::Linear;
    FilterMode  mipFilter  = FilterMode::Linear;
    AddressMode addressU   = AddressMode::Repeat;
    AddressMode addressV   = AddressMode::Repeat;
    AddressMode addressW   = AddressMode::Repeat;
    float       mipLodBias = 0.0f;
    float       maxAnisotropy = 1.0f;
    float       minLod     = 0.0f;
    float       maxLod     = 1000.0f;
};

struct ShaderDesc {
    ShaderStage  stage      = ShaderStage::Vertex;
    ShaderFormat format     = ShaderFormat::SPIRV;
    const void*  bytecode   = nullptr;
    size_t       bytecodeSize = 0;
    const char*  entryPoint = "main";
};

struct VertexAttribute {
    uint32_t location = 0;
    uint32_t binding  = 0;
    uint32_t offset   = 0;
    // format encoded as element count + is_float, expand as needed
    uint32_t elementCount = 4; // e.g. 3 for vec3
    bool     isFloat      = true;
};

struct VertexBinding {
    uint32_t binding  = 0;
    uint32_t stride   = 0;
    bool     instanced = false;
};

struct ColorTargetBlend {
    bool      blendEnabled = false;
    BlendFactor srcColor   = BlendFactor::SrcAlpha;
    BlendFactor dstColor   = BlendFactor::OneMinusSrcAlpha;
    BlendOp     colorOp    = BlendOp::Add;
    BlendFactor srcAlpha   = BlendFactor::One;
    BlendFactor dstAlpha   = BlendFactor::Zero;
    BlendOp     alphaOp    = BlendOp::Add;
};

struct ColorTargetDesc {
    TextureFormat    format = TextureFormat::BGRA8Unorm;
    ColorTargetBlend blend  = {};
};

struct PipelineDesc {
    RHIShader                      vertexShader   = {};
    RHIShader                      fragmentShader = {};
    std::span<VertexAttribute>     vertexAttributes = {};
    std::span<VertexBinding>       vertexBindings   = {};
    std::span<ColorTargetDesc>     colorTargets     = {};
    TextureFormat                  depthFormat      = TextureFormat::D32Float;
    bool                           hasDepth         = true;
    PrimitiveTopology              topology         = PrimitiveTopology::TriangleList;
    CullMode                       cullMode         = CullMode::Back;
    FillMode                       fillMode         = FillMode::Solid;
    bool                           depthTest        = true;
    bool                           depthWrite       = true;
    CompareOp                      depthCompare     = CompareOp::Less;
};

struct ColorAttachment {
    RHITexture  texture    = {};         // invalid = swapchain backbuffer
    LoadOp      loadOp     = LoadOp::Clear;
    StoreOp     storeOp    = StoreOp::Store;
    float       clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthAttachment {
    RHITexture  texture      = {};
    LoadOp      loadOp       = LoadOp::Clear;
    StoreOp     storeOp      = StoreOp::DontCare;
    float       clearDepth   = 1.0f;
    uint8_t     clearStencil = 0;
};

struct RenderPassDesc {
    std::span<ColorAttachment> colorAttachments = {};
    const DepthAttachment*     depthAttachment  = nullptr;
};

// ---------------------------------------------------------------------------
// Device capabilities
// ---------------------------------------------------------------------------
struct RHICaps {
    bool     meshShaders       = false;
    bool     rayTracing        = false;
    bool     bindless          = false;
    uint32_t maxTextureDim     = 0;
    uint32_t maxPushConstBytes = 128;
    uint64_t deviceVRAMBytes   = 0;
};

} // namespace ds::rhi
