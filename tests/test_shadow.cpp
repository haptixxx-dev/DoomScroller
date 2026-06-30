#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include "engine/ShadowMatrix.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using Catch::Approx;
using namespace ds;

// A symmetric scene box centered on the origin: corners at +/-10 on each axis.
static Bounds makeBounds() {
    return Bounds{{-10.f, -10.f, -10.f}, {10.f, 10.f, 10.f}};
}

TEST_CASE("Bounds center is the box midpoint", "[shadow]") {
    Bounds b = makeBounds();
    REQUIRE(b.center().x == Approx(0.f).margin(1e-5f));
    REQUIRE(b.center().y == Approx(0.f).margin(1e-5f));
    REQUIRE(b.center().z == Approx(0.f).margin(1e-5f));
}

TEST_CASE("Bounds radius is half the space-diagonal length", "[shadow]") {
    // Diagonal of a 20x20x20 box = sqrt(20^2 * 3) = sqrt(1200); half = sqrt(300).
    Bounds b = makeBounds();
    REQUIRE(b.radius() == Approx(std::sqrt(300.f)).margin(1e-4f)); // ~17.3205
}

TEST_CASE("Straight-down sun: scene center maps to clip origin with [0,1] depth", "[shadow]") {
    // sunDir = (0,-1,0) is exactly the up-vector degeneracy case: the view
    // direction is parallel to the default (0,1,0) up reference, so this test
    // specifically exercises the guard that swaps to (0,0,1). If the guard were
    // missing, lookAt would produce NaNs and every assertion below would fail.
    Bounds b       = makeBounds();
    glm::mat4 ls   = sunLightSpaceMatrix(glm::vec3{0.f, -1.f, 0.f}, b);
    glm::vec4 clip = worldToShadowUV(ls, b.center());

    // Orthographic light: w == 1, so the perspective divide is a no-op.
    REQUIRE(clip.w == Approx(1.f).margin(1e-5f));
    glm::vec3 ndc = glm::vec3(clip) / clip.w;

    // Center of the scene projects to the center of the shadow map.
    REQUIRE(ndc.x == Approx(0.f).margin(1e-4f));
    REQUIRE(ndc.y == Approx(0.f).margin(1e-4f));

    // Depth in the [0,1] convention (GLM_FORCE_DEPTH_ZERO_TO_ONE).
    REQUIRE(ndc.z >= 0.f);
    REQUIRE(ndc.z <= 1.f);

    // Tight pin: the eye is backed off by (radius + zPadding) along the incoming
    // light, the far plane is at 2*radius + 2*zPadding, and the center sits at
    // depth (radius + zPadding) / (2*radius + 2*zPadding) = 0.5 exactly. A
    // corrupted near/far or eye offset would move this off 0.5.
    REQUIRE(ndc.z == Approx(0.5f).epsilon(1e-4f));
}

TEST_CASE("Straight-down sun: top corner stays within the [-1,1] clip range", "[shadow]") {
    Bounds b       = makeBounds();
    glm::mat4 ls   = sunLightSpaceMatrix(glm::vec3{0.f, -1.f, 0.f}, b);
    glm::vec4 clip = worldToShadowUV(ls, b.max); // (10,10,10), a top corner
    glm::vec3 ndc  = glm::vec3(clip) / clip.w;

    REQUIRE(ndc.x >= -1.f);
    REQUIRE(ndc.x <= 1.f);
    REQUIRE(ndc.y >= -1.f);
    REQUIRE(ndc.y <= 1.f);
    REQUIRE(ndc.z >= 0.f);
    REQUIRE(ndc.z <= 1.f);

    // Pin the orthographic extent == radius: with a straight-down sun the light
    // basis maps world x to clip x up to a sign (the basis handedness fixes which
    // side), so |ndc.x| = 10 / radius = 10 / sqrt(300) ~= 0.5774. A corrupted
    // ortho extent (e.g. a fudged radius scale) would move this magnitude.
    REQUIRE(std::abs(ndc.x) == Approx(10.f / std::sqrt(300.f)).epsilon(1e-2f));
}

TEST_CASE("Light-space matrix is finite (no NaN/Inf) for the vertical sun", "[shadow]") {
    Bounds b     = makeBounds();
    glm::mat4 ls = sunLightSpaceMatrix(glm::vec3{0.f, -1.f, 0.f}, b);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            REQUIRE(std::isfinite(ls[col][row]));
        }
    }
}

TEST_CASE("Zero sun direction falls back to a finite matrix", "[shadow]") {
    // A ~zero sunDir cannot be normalized; without the guard glm::normalize
    // divides by zero and floods the matrix with NaNs. The fallback to a default
    // downward direction (0,-1,0) must keep every element finite.
    Bounds b     = makeBounds();
    glm::mat4 ls = sunLightSpaceMatrix(glm::vec3{0.f}, b);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            REQUIRE(std::isfinite(ls[col][row]));
        }
    }
}

TEST_CASE("Angled sun still covers the scene center in clip range", "[shadow]") {
    Bounds b       = makeBounds();
    glm::mat4 ls   = sunLightSpaceMatrix(glm::vec3{-1.f, -1.f, -0.5f}, b);
    glm::vec4 clip = worldToShadowUV(ls, b.center());
    glm::vec3 ndc  = glm::vec3(clip) / clip.w;

    REQUIRE(ndc.x == Approx(0.f).margin(1e-4f));
    REQUIRE(ndc.y == Approx(0.f).margin(1e-4f));
    REQUIRE(ndc.z >= 0.f);
    REQUIRE(ndc.z <= 1.f);
}
