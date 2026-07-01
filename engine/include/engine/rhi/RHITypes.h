#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ds::rhi {

// ---------------------------------------------------------------------------
// Opaque handles — backends store actual GPU objects behind these
// ---------------------------------------------------------------------------
template<typename Tag> struct Handle {
    void* ptr = nullptr;
    bool valid() const { return ptr != nullptr; }
};

struct BufferTag {};
struct TextureTag {};
struct SamplerTag {};
struct ShaderTag {};
struct PipelineTag {};
struct ComputePipelineTag {};

using RHIBuffer          = Handle<BufferTag>;
using RHITexture         = Handle<TextureTag>;
using RHISampler         = Handle<SamplerTag>;
using RHIShader          = Handle<ShaderTag>;
using RHIPipeline        = Handle<PipelineTag>;
using RHIComputePipeline = Handle<ComputePipelineTag>;

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
enum class BufferUsage : uint32_t {
    Vertex  = 1 << 0,
    Index   = 1 << 1,
    Uniform = 1 << 2,
    Storage = 1 << 3, // compute storage read
    // Compute storage write (task 39): a buffer a compute shader writes to. The
    // GPU particle sim writes its instance buffer here, then it is read back as
    // a vertex buffer in the same frame (Vertex | StorageWrite).
    StorageWrite = 1 << 4,
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

enum class ShaderStage { Vertex, Fragment, Compute };
enum class ShaderFormat { SPIRV, MSL, DXIL };

// Base dimensionality of a texture (task 59). Tex2D is the historical default and
// reproduces the pre-59 behaviour byte-for-byte; TexCube (6 faces) and Tex2DArray
// (arrayLayers slices) unlock point-light cube shadows and the multi-light shadow
// array. Purely additive — existing 2D textures never set anything but Tex2D.
enum class TextureType : uint8_t { Tex2D, TexCube, Tex2DArray };

enum class PrimitiveTopology { TriangleList, TriangleStrip, LineList };
enum class IndexType { Uint16, Uint32 };
enum class CullMode { None, Front, Back };
enum class FillMode { Solid, Wireframe };
enum class CompareOp { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
};
enum class BlendOp { Add, Subtract, ReverseSubtract, Min, Max };

enum class FilterMode { Nearest, Linear };
enum class AddressMode { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder };

enum class LoadOp { Load, Clear, DontCare };
enum class StoreOp { Store, DontCare };

// ---------------------------------------------------------------------------
// Descriptors — plain data, no virtuals
// ---------------------------------------------------------------------------
struct BufferDesc {
    uint64_t size         = 0;
    BufferUsage usage     = BufferUsage::Vertex;
    bool cpuVisible       = false; // upload / readback
    const char* debugName = nullptr;
};

struct TextureDesc {
    uint32_t width     = 1;
    uint32_t height    = 1;
    uint32_t depth     = 1;
    uint32_t mipLevels = 1;
    // Dimensionality + slice count (task 59, additive). Defaults reproduce the
    // pre-59 2D single-layer behaviour exactly. For TexCube the backend uses 6
    // faces; for Tex2DArray it uses `arrayLayers` slices; for Tex2D it keeps
    // using `depth` as before.
    TextureType type      = TextureType::Tex2D;
    uint32_t arrayLayers  = 1;
    TextureFormat format  = TextureFormat::RGBA8Unorm;
    bool isRenderTarget   = false;
    bool isDepthStencil   = false;
    const char* debugName = nullptr;
};

struct SamplerDesc {
    FilterMode minFilter = FilterMode::Linear;
    FilterMode magFilter = FilterMode::Linear;
    FilterMode mipFilter = FilterMode::Linear;
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
    AddressMode addressW = AddressMode::Repeat;
    float mipLodBias     = 0.0f;
    float maxAnisotropy  = 1.0f;
    float minLod         = 0.0f;
    float maxLod         = 1000.0f;
};

struct ShaderDesc {
    ShaderStage stage           = ShaderStage::Vertex;
    ShaderFormat format         = ShaderFormat::SPIRV;
    const void* bytecode        = nullptr;
    size_t bytecodeSize         = 0;
    const char* entryPoint      = "main";
    uint32_t numSamplers        = 0;
    uint32_t numUniformBuffers  = 0;
    uint32_t numStorageBuffers  = 0;
    uint32_t numStorageTextures = 0;
};

struct VertexAttribute {
    uint32_t location = 0;
    uint32_t binding  = 0;
    uint32_t offset   = 0;
    // format encoded as element count + is_float, expand as needed
    uint32_t elementCount = 4; // e.g. 3 for vec3
    bool isFloat          = true;
};

struct VertexBinding {
    uint32_t binding = 0;
    uint32_t stride  = 0;
    bool instanced   = false;
};

struct ColorTargetBlend {
    bool blendEnabled    = false;
    BlendFactor srcColor = BlendFactor::SrcAlpha;
    BlendFactor dstColor = BlendFactor::OneMinusSrcAlpha;
    BlendOp colorOp      = BlendOp::Add;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::Zero;
    BlendOp alphaOp      = BlendOp::Add;
};

struct ColorTargetDesc {
    TextureFormat format   = TextureFormat::BGRA8Unorm;
    ColorTargetBlend blend = {};
};

struct PipelineDesc {
    RHIShader vertexShader   = {};
    RHIShader fragmentShader = {};
    std::span<VertexAttribute> vertexAttributes;
    std::span<VertexBinding> vertexBindings;
    std::span<ColorTargetDesc> colorTargets;
    TextureFormat depthFormat  = TextureFormat::D32Float;
    bool hasDepth              = true;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CullMode cullMode          = CullMode::Back;
    FillMode fillMode          = FillMode::Solid;
    bool depthTest             = true;
    bool depthWrite            = true;
    CompareOp depthCompare     = CompareOp::Less;
};

// Compute pipeline (task 39). Mirrors the resource counts SDL3 needs to bind a
// compute shader: read-only storage buffers, read-write storage buffers, and
// (unused for the particle sim) storage textures + uniform buffers. The shader
// bytecode is supplied directly (entry `compMain`) rather than via an RHIShader
// because SDL3's compute pipeline create takes its own shader-create info.
struct ComputePipelineDesc {
    ShaderFormat format              = ShaderFormat::SPIRV;
    const void* bytecode             = nullptr;
    size_t bytecodeSize              = 0;
    const char* entryPoint           = "compMain";
    uint32_t numReadOnlyStorageBufs  = 0;
    uint32_t numReadWriteStorageBufs = 0;
    uint32_t numUniformBuffers       = 0;
    uint32_t threadCountX            = 64;
    uint32_t threadCountY            = 1;
    uint32_t threadCountZ            = 1;
    const char* debugName            = nullptr;
};

struct ColorAttachment {
    RHITexture texture  = {}; // invalid = swapchain backbuffer
    LoadOp loadOp       = LoadOp::Clear;
    StoreOp storeOp     = StoreOp::Store;
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    // Target sub-resource (task 59, additive): render into layer/face `layer` and
    // mip `mipLevel` of the texture. Default 0/0 targets the first layer at mip 0,
    // i.e. the pre-59 behaviour. `layer` selects the array slice on a Tex2DArray
    // or the face (0..5) on a TexCube.
    uint32_t layer    = 0;
    uint32_t mipLevel = 0;
};

struct DepthAttachment {
    RHITexture texture   = {};
    LoadOp loadOp        = LoadOp::Clear;
    StoreOp storeOp      = StoreOp::DontCare;
    float clearDepth     = 1.0f;
    uint8_t clearStencil = 0;
    // Target sub-resource (task 59, additive): default 0/0 preserves current
    // behaviour. See ColorAttachment above.
    uint32_t layer    = 0;
    uint32_t mipLevel = 0;
};

struct RenderPassDesc {
    std::span<ColorAttachment> colorAttachments;
    const DepthAttachment* depthAttachment = nullptr;
};

// ---------------------------------------------------------------------------
// Device capabilities
// ---------------------------------------------------------------------------
struct RHICaps {
    bool meshShaders           = false;
    bool rayTracing            = false;
    bool bindless              = false;
    uint32_t maxTextureDim     = 0;
    uint32_t maxPushConstBytes = 128;
    uint64_t deviceVRAMBytes   = 0;
};

} // namespace ds::rhi
