#pragma once

#include "engine/rhi/RHITypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <span>
#include <vector>

namespace ds {

// ---------------------------------------------------------------------------
// GPU-instanced batching (pure grouping logic).
//
// The renderer draws one entity per drawIndexed today; that is one draw call
// per object. Task 28 collapses every entity that shares the same geometry
// (vertex + index buffer) and the same surface texture (albedo) into a single
// instanced draw. The hot, side-effect-free piece is the grouping itself,
// isolated here so it can be unit-tested without a GPU.
// ---------------------------------------------------------------------------

// Identity of a batchable draw: the opaque RHI handle pointers for the mesh's
// vertex/index buffers and the material's albedo texture. Two entities batch
// together iff all three pointers match — i.e. they reference the same GPU
// objects. Sampler/PBR scalars are intentionally excluded: they are pushed as
// constants, not part of the geometry/texture binding, and folding them in
// would over-fragment batches. Pointers are compared by identity, never
// dereferenced.
struct InstanceKey {
    void* vertexBuffer = nullptr;
    void* indexBuffer  = nullptr;
    void* albedo       = nullptr;

    bool operator==(const InstanceKey& o) const {
        return vertexBuffer == o.vertexBuffer && indexBuffer == o.indexBuffer && albedo == o.albedo;
    }
    bool operator!=(const InstanceKey& o) const { return !(*this == o); }
};

// Hash over the three handle pointers so InstanceKey can key an unordered_map.
struct InstanceKeyHash {
    std::size_t operator()(const InstanceKey& k) const {
        std::size_t h = std::hash<void*>{}(k.vertexBuffer);
        // 0x9e3779b97f4a7c15 = 64-bit golden-ratio constant; the rotate-and-mix
        // is the usual boost-style combiner so distinct fields don't cancel.
        h ^= std::hash<void*>{}(k.indexBuffer) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<void*>{}(k.albedo) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

// One entity's contribution to a batch: its key plus the model matrix to draw
// it with. indexCount/indexType ride along so the resulting batch knows how to
// issue drawIndexed without re-touching the source components.
struct InstanceDraw {
    InstanceKey key;
    uint32_t indexCount      = 0;
    rhi::IndexType indexType = rhi::IndexType::Uint16;
    glm::mat4 model{1.f};
};

// A coalesced draw: every InstanceDraw that shared `key`, with the per-instance
// model matrices concatenated in encounter order. The renderer issues exactly
// one drawIndexed(indexCount, instanceCount = models.size()) per DrawBatch.
struct DrawBatch {
    InstanceKey key;
    uint32_t indexCount      = 0;
    rhi::IndexType indexType = rhi::IndexType::Uint16;
    std::vector<glm::mat4> models;
};

// Group `draws` by InstanceKey, concatenating model matrices into one batch per
// distinct key. Pure: no GPU, no allocation beyond the result.
//
// Guarantees:
//  - One DrawBatch per distinct key.
//  - Within a batch, models are in the order the matching draws appear in the
//    input (stable) — the renderer relies on this for deterministic output.
//  - Batches themselves are ordered by first appearance of their key.
//  - Empty input -> empty output.
// indexCount/indexType are taken from the first draw seen for a key; callers are
// expected to keep these consistent across draws that share a key (they come
// from the same MeshComponent, so they will).
inline std::vector<DrawBatch> buildBatches(std::span<const InstanceDraw> draws) {
    std::vector<DrawBatch> batches;
    for (const InstanceDraw& d : draws) {
        DrawBatch* target = nullptr;
        for (DrawBatch& b : batches) {
            if (b.key == d.key) {
                target = &b;
                break;
            }
        }
        if (target == nullptr) {
            DrawBatch batch;
            batch.key        = d.key;
            batch.indexCount = d.indexCount;
            batch.indexType  = d.indexType;
            batches.push_back(std::move(batch));
            target = &batches.back();
        }
        target->models.push_back(d.model);
    }
    return batches;
}

} // namespace ds
