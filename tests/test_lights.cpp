#include "engine/ecs/Components.h"
#include "engine/ecs/RenderSystem.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <glm/glm.hpp>

using namespace ds;

TEST_CASE("LightComponent default values", "[lights]") {
    LightComponent l;
    REQUIRE(l.position == glm::vec3(0.f));
    REQUIRE(l.color == glm::vec3(1.f));
    REQUIRE(l.radius == 8.f);
    REQUIRE(l.intensity == 1.f);
}

TEST_CASE("GpuLight is std140-friendly (32 bytes, two vec4s)", "[lights]") {
    REQUIRE(sizeof(GpuLight) == 32);
    REQUIRE(offsetof(GpuLight, posRadius) == 0);
    REQUIRE(offsetof(GpuLight, colorIntensity) == 16);
}

TEST_CASE("LightBuffer matches the shader block layout", "[lights]") {
    // 16-byte header (count + 3 pad ints) then the light array.
    REQUIRE(offsetof(LightBuffer, count) == 0);
    REQUIRE(offsetof(LightBuffer, lights) == 16);
    REQUIRE(kMaxForwardLights == 16);
    // Total size = header + N * 32. Must stay within typical push-constant caps.
    REQUIRE(sizeof(LightBuffer) == 16 + kMaxForwardLights * 32);
}

// Mirrors Engine::updateLights packing so the gather logic is unit-testable
// without the full engine (SDL3/Jolt) link.
static int packLight(LightBuffer& buf, int count, const glm::vec3& pos, const glm::vec3& color, float radius,
                     float intensity) {
    if (count >= kMaxForwardLights)
        return count;
    buf.lights[count].posRadius      = glm::vec4(pos, radius);
    buf.lights[count].colorIntensity = glm::vec4(color, intensity);
    return count + 1;
}

TEST_CASE("Light packing stores position+radius and color+intensity", "[lights]") {
    LightBuffer buf{};
    int count = 0;
    count     = packLight(buf, count, {1.f, 2.f, 3.f}, {0.5f, 0.6f, 0.7f}, 9.f, 4.f);
    buf.count = count;

    REQUIRE(buf.count == 1);
    REQUIRE(buf.lights[0].posRadius == glm::vec4(1.f, 2.f, 3.f, 9.f));
    REQUIRE(buf.lights[0].colorIntensity == glm::vec4(0.5f, 0.6f, 0.7f, 4.f));
}

TEST_CASE("Light packing is capped at kMaxForwardLights", "[lights]") {
    LightBuffer buf{};
    int count = 0;
    for (int i = 0; i < kMaxForwardLights + 8; ++i)
        count = packLight(buf, count, glm::vec3(static_cast<float>(i)), glm::vec3(1.f), 4.f, 1.f);
    REQUIRE(count == kMaxForwardLights);
}
