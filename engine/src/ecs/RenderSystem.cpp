#include "engine/ecs/RenderSystem.h"

#include "engine/ecs/Components.h"

namespace ds {

void renderSystem(entt::registry& world, const RenderContext& ctx) {
    ctx.cmd->setPipeline(ctx.pipeline);

    // Forward lights live in fragment uniform slot 0 and are constant for the
    // whole pass, so push them once up front.
    if (ctx.lights)
        ctx.cmd->pushFragmentConstants(ctx.lights, sizeof(LightBuffer));

    auto view = world.view<Transform, MeshComponent, MaterialComponent>();
    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& mesh      = view.get<MeshComponent>(entity);
        auto& material  = view.get<MaterialComponent>(entity);

        glm::mat4 model = transform.modelMatrix();
        struct {
            glm::mat4 mvp;
            glm::mat4 model;
        } push{ctx.viewProj * model, model};
        ctx.cmd->pushVertexConstants(&push, sizeof(push));

        ctx.cmd->setVertexBuffer(0, mesh.vertexBuffer);
        ctx.cmd->setIndexBuffer(mesh.indexBuffer, mesh.indexType);
        ctx.cmd->bindFragmentTexture(0, material.albedo, material.sampler);
        ctx.cmd->drawIndexed(mesh.indexCount);
    }
}

} // namespace ds
