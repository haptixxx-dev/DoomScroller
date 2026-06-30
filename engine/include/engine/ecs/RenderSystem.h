#pragma once

#include "engine/Frustum.h"
#include "engine/rhi/IRHICommandList.h"
#include "engine/rhi/IRHIDevice.h"
#include "engine/rhi/RHITypes.h"

#include <cstdint>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace ds {

// Maximum number of point lights forwarded to the mesh fragment shader in a
// single frame. Must match the array size in shaders/mesh.slang.
inline constexpr int kMaxForwardLights = 16;

// std140-friendly point light: posRadius = (x,y,z, radius),
// colorIntensity = (r,g,b, intensity). 32 bytes, 16-byte aligned.
struct GpuLight {
    glm::vec4 posRadius{0.f};
    glm::vec4 colorIntensity{0.f};
};

// CPU mirror of the fragment uniform block in mesh.slang. The leading vec4
// keeps `count` 16-byte aligned (std140) ahead of the light array.
struct LightBuffer {
    int32_t count = 0;
    int32_t pad0  = 0;
    int32_t pad1  = 0;
    int32_t pad2  = 0;
    GpuLight lights[kMaxForwardLights]{};
};

// CPU mirror of the per-entity fragment material block in mesh.slang (fragment
// uniform slot 1, register(b1, space3)). std140-aligned: a leading vec4 carries
// the scalar metallic/roughness plus the camera world position (needed for the
// view vector V in the BRDF), followed by the base color tint.
struct MaterialBuffer {
    float metallic  = 0.f;
    float roughness = 0.8f;
    float pad0      = 0.f;
    float pad1      = 0.f;
    glm::vec3 cameraPos{0.f};
    float pad2 = 0.f;
    glm::vec3 baseColorTint{1.f};
    float pad3 = 0.f;
};

struct RenderContext {
    rhi::IRHICommandList* cmd;
    rhi::RHIPipeline pipeline;
    glm::mat4 viewProj;
    glm::vec3 cameraPos{0.f};            // world-space eye, for the BRDF view vector
    const LightBuffer* lights = nullptr; // optional; bound once before drawing
    const Frustum* frustum    = nullptr; // optional; when set, dynamic entities outside it are culled (task 24)

    // --- GPU instancing (task 28 full path). ------------------------------
    // Per-frame, device-resident vertex buffer (binding slot 1, instanced) into
    // which renderSystem uploads every batch's model matrices back-to-back, then
    // issues ONE drawIndexed(indexCount, instanceCount) per batch reading the
    // matrices at the batch's byte offset. `device` performs the upload;
    // `instanceCapacity` is the buffer's capacity in mat4 elements (draws beyond
    // it are clamped so the upload never overruns). When `instanceBuffer` is
    // invalid the renderer must not be used (the mesh pipeline requires the
    // instance stream). The mesh vertex shader assembles model from this stream
    // and computes mvp = viewProj * model in-shader, so viewProj is pushed as
    // the vertex uniform instead of a per-draw mvp+model.
    rhi::IRHIDevice* device       = nullptr;
    rhi::RHIBuffer instanceBuffer = {};
    uint32_t instanceCapacity     = 0;
};

// Iterates entities with Transform + MeshComponent + MaterialComponent and draws each.
void renderSystem(entt::registry& world, const RenderContext& ctx);

} // namespace ds
