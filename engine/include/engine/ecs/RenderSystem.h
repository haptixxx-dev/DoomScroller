#pragma once

#include "engine/rhi/IRHICommandList.h"
#include "engine/rhi/RHITypes.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace ds {

struct RenderContext {
    rhi::IRHICommandList* cmd;
    rhi::RHIPipeline      pipeline;
    glm::mat4             viewProj;
};

// Iterates entities with Transform + MeshComponent + MaterialComponent and draws each.
void renderSystem(entt::registry& world, const RenderContext& ctx);

} // namespace ds
