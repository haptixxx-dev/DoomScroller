#include "engine/MaterialTable.h"
#include "engine/ecs/Components.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using namespace ds;

// Pure CPU material-index packing for bindless (task 58). Links
// engine_math/engine_headers only: MaterialTable.h + MaterialComponent are
// header-only and treat RHI handles as opaque pointers (never dereferenced), so
// no GPU / SDL3 / Jolt state is touched.

namespace {

// Fabricate an opaque texture handle from an integer id — the table only ever
// compares/hashes the pointer, it never dereferences it, so a fake address is
// a valid distinct-texture stand-in.
rhi::RHITexture fakeTexture(std::uintptr_t id) {
    rhi::RHITexture t{};
    t.ptr = reinterpret_cast<void*>(id);
    return t;
}

MaterialComponent makeMaterial(std::uintptr_t albedoId, float metallic, float roughness, glm::vec3 tint) {
    MaterialComponent m{};
    m.albedo        = fakeTexture(albedoId);
    m.metallic      = metallic;
    m.roughness     = roughness;
    m.baseColorTint = tint;
    return m;
}

} // namespace

TEST_CASE("identical materials share one index", "[material]") {
    MaterialTable table;
    const MaterialComponent a = makeMaterial(0x1000, 0.2f, 0.5f, {1.f, 0.f, 0.f});
    const MaterialComponent b = makeMaterial(0x1000, 0.2f, 0.5f, {1.f, 0.f, 0.f}); // same key

    const uint32_t ia = table.indexOf(a);
    const uint32_t ib = table.indexOf(b);

    REQUIRE(ia == ib);
    REQUIRE(ia == 0u);          // first material gets slot 0
    REQUIRE(table.size() == 1); // deduped: only one distinct material
}

TEST_CASE("distinct materials get distinct indices", "[material]") {
    MaterialTable table;
    // Differ in exactly one field each — every axis of the key must fragment.
    const MaterialComponent base    = makeMaterial(0x1000, 0.2f, 0.5f, {1.f, 1.f, 1.f});
    const MaterialComponent difTex  = makeMaterial(0x2000, 0.2f, 0.5f, {1.f, 1.f, 1.f});
    const MaterialComponent difMet  = makeMaterial(0x1000, 0.9f, 0.5f, {1.f, 1.f, 1.f});
    const MaterialComponent difRgh  = makeMaterial(0x1000, 0.2f, 0.1f, {1.f, 1.f, 1.f});
    const MaterialComponent difTint = makeMaterial(0x1000, 0.2f, 0.5f, {0.f, 1.f, 1.f});

    const uint32_t i0 = table.indexOf(base);
    const uint32_t i1 = table.indexOf(difTex);
    const uint32_t i2 = table.indexOf(difMet);
    const uint32_t i3 = table.indexOf(difRgh);
    const uint32_t i4 = table.indexOf(difTint);

    // All five are distinct materials -> five distinct slots.
    REQUIRE(table.size() == 5);
    const uint32_t all[] = {i0, i1, i2, i3, i4};
    for (int a = 0; a < 5; ++a)
        for (int b = a + 1; b < 5; ++b)
            REQUIRE(all[a] != all[b]);
}

TEST_CASE("index order is stable and deterministic (first-seen order)", "[material]") {
    // The same sequence of materials always yields the same indices, handed out
    // in first-seen order starting at 0.
    const MaterialComponent red   = makeMaterial(0x1000, 0.f, 0.8f, {1.f, 0.f, 0.f});
    const MaterialComponent green = makeMaterial(0x2000, 0.f, 0.8f, {0.f, 1.f, 0.f});
    const MaterialComponent blue  = makeMaterial(0x3000, 0.f, 0.8f, {0.f, 0.f, 1.f});

    const std::vector<MaterialComponent> seq = {red, green, red, blue, green, red};

    MaterialTable t1;
    const std::vector<uint32_t> idx1 = packMaterialIndices(t1, seq);

    // red=0 (first seen), green=1, blue=2; repeats reuse their slot.
    REQUIRE(idx1 == std::vector<uint32_t>{0u, 1u, 0u, 2u, 1u, 0u});
    REQUIRE(t1.size() == 3);

    // Re-running from a fresh table over the same sequence reproduces it exactly.
    MaterialTable t2;
    const std::vector<uint32_t> idx2 = packMaterialIndices(t2, seq);
    REQUIRE(idx2 == idx1);

    // The distinct-key readback is in slot order.
    REQUIRE(t1.keys().size() == 3);
    REQUIRE(t1.keys()[0] == MaterialKey::fromComponent(red));
    REQUIRE(t1.keys()[1] == MaterialKey::fromComponent(green));
    REQUIRE(t1.keys()[2] == MaterialKey::fromComponent(blue));
}

TEST_CASE("clear resets the table for reuse across frames", "[material]") {
    MaterialTable table;
    const MaterialComponent a = makeMaterial(0x1000, 0.2f, 0.5f, {1.f, 0.f, 0.f});
    REQUIRE(table.empty());
    (void)table.indexOf(a);
    REQUIRE_FALSE(table.empty());
    REQUIRE(table.size() == 1);

    table.clear();
    REQUIRE(table.empty());
    REQUIRE(table.size() == 0);
    // After clear the next distinct material starts again at slot 0.
    REQUIRE(table.indexOf(a) == 0u);
}
