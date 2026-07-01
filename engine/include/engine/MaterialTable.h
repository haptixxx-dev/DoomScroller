#pragma once

#include "engine/ecs/Components.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace ds {

// ---------------------------------------------------------------------------
// Material-index packing (task 58 bindless, CPU side).
//
// Bindless materials replace per-draw texture binding with a single texture
// array + a per-instance material index into a parallel material-parameter
// array. Before any GPU array binding exists, the CPU work is pure: dedup the
// distinct MaterialComponents a frame references and assign each a stable slot,
// then emit a per-instance material-index array. This header is that pure seam —
// no RHI calls, handles treated as opaque pointers (compared/hashed by identity,
// never dereferenced) — so it lands + tests headless ahead of the GPU array.
// ---------------------------------------------------------------------------

// The identity of a distinct material for bindless slotting: the albedo texture
// handle plus the scalar PBR params (metallic, roughness) and the base-color
// tint. Two materials share a bindless slot iff all of these match. The sampler
// is intentionally excluded (samplers are a tiny fixed set bound globally, not
// per-material-slot); folding it in would over-fragment the table. Floats are
// compared/hashed by their exact bit pattern so the dedup is deterministic and
// value-stable (no epsilon fuzz — two materials authored with the same literal
// values collapse, distinct literals do not).
struct MaterialKey {
    const void* albedo = nullptr; // rhi::RHITexture handle pointer, opaque
    float metallic     = 0.f;
    float roughness    = 0.8f;
    glm::vec3 tint     = glm::vec3(1.f);

    static MaterialKey fromComponent(const MaterialComponent& m) {
        return MaterialKey{m.albedo.ptr, m.metallic, m.roughness, m.baseColorTint};
    }

    bool operator==(const MaterialKey& o) const {
        return albedo == o.albedo && bits(metallic) == bits(o.metallic) && bits(roughness) == bits(o.roughness) &&
               bits(tint.x) == bits(o.tint.x) && bits(tint.y) == bits(o.tint.y) && bits(tint.z) == bits(o.tint.z);
    }

    // Reinterpret a float as its 32-bit pattern (deterministic exact compare).
    static uint32_t bits(float f) {
        uint32_t u = 0;
        std::memcpy(&u, &f, sizeof(u));
        return u;
    }
};

// Hash over the albedo pointer + the float bit patterns (boost-style combine).
struct MaterialKeyHash {
    std::size_t operator()(const MaterialKey& k) const {
        std::size_t h = std::hash<const void*>{}(k.albedo);
        auto mix      = [&h](uint32_t v) {
            // 0x9e3779b97f4a7c15 = 64-bit golden-ratio constant (same combiner
            // as InstanceKeyHash) so distinct fields don't cancel.
            h ^= std::hash<uint32_t>{}(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        mix(MaterialKey::bits(k.metallic));
        mix(MaterialKey::bits(k.roughness));
        mix(MaterialKey::bits(k.tint.x));
        mix(MaterialKey::bits(k.tint.y));
        mix(MaterialKey::bits(k.tint.z));
        return h;
    }
};

// Assigns each distinct material a stable, deterministic index (its slot in a
// future bindless material array) and dedups repeats. Indices are handed out in
// first-seen order starting at 0, so the same sequence of materials always
// produces the same indices. Pure: no GPU, no RHI calls.
class MaterialTable {
  public:
    // Return the slot index for `m`, assigning a fresh index (the current size)
    // the first time a distinct material is seen and reusing it thereafter.
    uint32_t indexOf(const MaterialComponent& m) {
        const MaterialKey key = MaterialKey::fromComponent(m);
        auto it               = m_lookup.find(key);
        if (it != m_lookup.end())
            return it->second;
        const uint32_t idx = static_cast<uint32_t>(m_keys.size());
        m_lookup.emplace(key, idx);
        m_keys.push_back(key);
        return idx;
    }

    // Number of distinct materials assigned so far.
    std::size_t size() const { return m_keys.size(); }

    // True if no materials have been assigned yet.
    bool empty() const { return m_keys.empty(); }

    // The distinct material keys in slot order (slot i == m_keys[i]).
    const std::vector<MaterialKey>& keys() const { return m_keys; }

    // Drop all assignments (reuse the table across frames without realloc churn).
    void clear() {
        m_lookup.clear();
        m_keys.clear();
    }

  private:
    std::unordered_map<MaterialKey, uint32_t, MaterialKeyHash> m_lookup;
    std::vector<MaterialKey> m_keys; // slot order, for deterministic readback
};

// Build the per-instance material-index array for a run of materials, in input
// order, deduping via `table` (which is populated as a side effect so callers
// can read back the distinct-material set afterwards). instanceMaterials[i] is
// the bindless slot for materials[i]. Pure.
inline std::vector<uint32_t> packMaterialIndices(MaterialTable& table,
                                                 const std::vector<MaterialComponent>& materials) {
    std::vector<uint32_t> indices;
    indices.reserve(materials.size());
    for (const MaterialComponent& m : materials)
        indices.push_back(table.indexOf(m));
    return indices;
}

} // namespace ds
