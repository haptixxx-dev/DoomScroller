#include "engine/ecs/RenderSystem.h"

#include "engine/Frustum.h"
#include "engine/InstanceBatch.h"
#include "engine/ecs/Components.h"

#include <unordered_map>
#include <vector>

namespace ds {

namespace {

// Conservative per-entity world bound for frustum culling (task 24). Dynamic
// gameplay actors (enemies, projectiles, gibs) are small, so a fixed box around
// their world position is a safe over-estimate. Static scene geometry (walls /
// floor / level meshes) is NEVER culled here — its mesh extents are unknown to
// this system and a wrong cull would punch holes in the arena shell — so those
// entities skip the test entirely and always draw.
constexpr float kDynamicBound = 1.5f;

bool isDynamic(const entt::registry& world, entt::entity e) {
    return world.any_of<EnemyComponent, ProjectileComponent>(e);
}

} // namespace

void renderSystem(entt::registry& world, const RenderContext& ctx) {
    ctx.cmd->setPipeline(ctx.pipeline);

    // The mesh vertex shader now assembles the per-instance model matrix from
    // the instanced vertex stream and computes mvp = viewProj * model itself, so
    // the only per-pass vertex uniform is viewProj (pushed once, slot 0).
    ctx.cmd->pushVertexConstants(&ctx.viewProj, sizeof(ctx.viewProj));

    // Forward lights live in fragment uniform slot 0 and are constant for the
    // whole pass, so push them once up front.
    if (ctx.lights)
        ctx.cmd->pushFragmentConstants(ctx.lights, sizeof(LightBuffer));

    // --- Collect draws (task 28 grouping + task 24 culling). ----------------
    // Each surviving entity becomes an InstanceDraw keyed by its mesh + albedo
    // pointers; buildBatches() coalesces entities that share all three handles
    // so the pipeline/texture/material binds happen once per batch instead of
    // once per entity. We retain a parallel material list per batch (the albedo
    // is part of the key, so a batch's material is uniform) for the per-batch
    // texture + PBR push.
    std::vector<InstanceDraw> draws;
    // Side-table from InstanceKey -> the MaterialComponent/sampler to bind for
    // that batch (first one seen wins; albedo is keyed, the rest are ~uniform).
    std::unordered_map<InstanceKey, MaterialComponent, InstanceKeyHash> batchMaterial;
    // Mesh handles per key so the batch can bind vertex/index buffers.
    std::unordered_map<InstanceKey, MeshComponent, InstanceKeyHash> batchMesh;

    auto view = world.view<Transform, MeshComponent, MaterialComponent>();
    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& mesh      = view.get<MeshComponent>(entity);
        auto& material  = view.get<MaterialComponent>(entity);

        // Frustum cull dynamic actors whose bound is fully outside the view.
        if (ctx.frustum && isDynamic(world, entity)) {
            AABB box = aabbFromCenterExtents(transform.position, glm::vec3(kDynamicBound));
            if (!aabbInFrustum(*ctx.frustum, box))
                continue;
        }

        InstanceKey key{mesh.vertexBuffer.ptr, mesh.indexBuffer.ptr, material.albedo.ptr};
        InstanceDraw d;
        d.key        = key;
        d.indexCount = mesh.indexCount;
        d.indexType  = mesh.indexType;
        d.model      = transform.modelMatrix();
        draws.push_back(d);

        batchMaterial.try_emplace(key, material);
        batchMesh.try_emplace(key, mesh);
    }

    std::vector<DrawBatch> batches = buildBatches(draws);

    // --- Full GPU instancing (task 28). ------------------------------------
    // Concatenate every batch's model matrices into one CPU buffer in batch
    // order, recording each batch's starting instance index, then upload the
    // whole block once and issue ONE drawIndexed(indexCount, instanceCount) per
    // batch reading from its slice of the instanced vertex stream (binding 1).
    //
    // The shared instance buffer has a fixed capacity (ctx.instanceCapacity
    // mat4s); if a frame somehow exceeds it the tail batches are clamped so the
    // upload + per-instance reads stay in-bounds (visual loss only, never a
    // buffer overrun). The Engine sizes the buffer generously, so this is a
    // safety net rather than an expected path.
    std::vector<glm::mat4> instanceData;
    std::vector<uint32_t> batchFirstInstance(batches.size(), 0);
    std::vector<uint32_t> batchInstanceCount(batches.size(), 0);
    const uint32_t capacity = ctx.instanceCapacity;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        batchFirstInstance[bi] = static_cast<uint32_t>(instanceData.size());
        for (const glm::mat4& model : batches[bi].models) {
            if (static_cast<uint32_t>(instanceData.size()) >= capacity)
                break; // out of instance-buffer space; drop the overflow tail.
            instanceData.push_back(model);
        }
        batchInstanceCount[bi] = static_cast<uint32_t>(instanceData.size()) - batchFirstInstance[bi];
    }

    if (instanceData.empty())
        return;

    // Single upload of the whole instance block (offset 0).
    if (ctx.device != nullptr && ctx.instanceBuffer.valid()) {
        ctx.device->uploadImmediate(ctx.instanceBuffer, instanceData.data(), instanceData.size() * sizeof(glm::mat4),
                                    0);
    }

    constexpr uint64_t kMat4Size = sizeof(glm::mat4);
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const DrawBatch& batch = batches[bi];
        if (batchInstanceCount[bi] == 0)
            continue;
        const MeshComponent& mesh     = batchMesh[batch.key];
        const MaterialComponent& matl = batchMaterial[batch.key];

        // Per-batch material PBR push (fragment slot 1) + albedo bind. Uniform
        // within the batch because the albedo handle is part of the key.
        MaterialBuffer mat{};
        mat.metallic      = matl.metallic;
        mat.roughness     = matl.roughness;
        mat.cameraPos     = ctx.cameraPos;
        mat.baseColorTint = matl.baseColorTint;
        ctx.cmd->pushFragmentConstants(&mat, sizeof(mat), 1);

        // Geometry at slot 0; the per-instance model matrices at slot 1, offset
        // to this batch's slice. firstInstance is baked into the buffer offset
        // (SDL3 GPU instance addressing starts at the bound buffer's base), so
        // drawIndexed's firstInstance stays 0.
        ctx.cmd->setVertexBuffer(0, mesh.vertexBuffer);
        ctx.cmd->setVertexBuffer(1, ctx.instanceBuffer, batchFirstInstance[bi] * kMat4Size);
        ctx.cmd->setIndexBuffer(mesh.indexBuffer, mesh.indexType);
        ctx.cmd->bindFragmentTexture(0, matl.albedo, matl.sampler);

        ctx.cmd->drawIndexed(batch.indexCount, batchInstanceCount[bi], 0, 0, 0);
    }
}

} // namespace ds
