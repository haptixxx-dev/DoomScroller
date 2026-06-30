#pragma once

#include "engine/rhi/IRHICommandList.h"
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

struct RenderContext {
    rhi::IRHICommandList* cmd;
    rhi::RHIPipeline pipeline;
    glm::mat4 viewProj;
    const LightBuffer* lights = nullptr; // optional; bound once before drawing
};

// Iterates entities with Transform + MeshComponent + MaterialComponent and draws each.
void renderSystem(entt::registry& world, const RenderContext& ctx);

} // namespace ds
