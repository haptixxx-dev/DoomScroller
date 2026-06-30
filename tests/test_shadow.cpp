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

// --- Point-light cube-face shadow matrices ---------------------------------

TEST_CASE("cubeFaceDirection returns the 6 conventional axis directions", "[shadow][point]") {
    REQUIRE(cubeFaceDirection(0) == glm::vec3{1.f, 0.f, 0.f});
    REQUIRE(cubeFaceDirection(1) == glm::vec3{-1.f, 0.f, 0.f});
    REQUIRE(cubeFaceDirection(2) == glm::vec3{0.f, 1.f, 0.f});
    REQUIRE(cubeFaceDirection(3) == glm::vec3{0.f, -1.f, 0.f});
    REQUIRE(cubeFaceDirection(4) == glm::vec3{0.f, 0.f, 1.f});
    REQUIRE(cubeFaceDirection(5) == glm::vec3{0.f, 0.f, -1.f});
}

TEST_CASE("cubeFaceUp is never parallel to that face's direction", "[shadow][point]") {
    for (int face = 0; face < 6; ++face) {
        glm::vec3 dir = cubeFaceDirection(face);
        glm::vec3 up  = cubeFaceUp(face);
        // A degenerate (parallel) up reference would make lookAt's basis
        // collapse (cross products go to zero) and poison the matrix with
        // NaNs — exactly the failure mode sunLightSpaceMatrix's up-guard
        // avoids for a straight-down sun.
        REQUIRE(std::abs(glm::dot(glm::normalize(dir), glm::normalize(up))) < 0.99f);
    }
}

TEST_CASE("pointShadowFaceMatrix: a point straight ahead on each face projects near clip center",
          "[shadow][point]") {
    glm::vec3 lightPos{2.f, 3.f, -1.f};
    constexpr float kNear = 0.1f;
    constexpr float kFar  = 20.f;

    for (int face = 0; face < 6; ++face) {
        glm::mat4 ls = pointShadowFaceMatrix(lightPos, face, kNear, kFar);
        // A point 5 units straight ahead along this face's direction.
        glm::vec3 aheadPoint = lightPos + cubeFaceDirection(face) * 5.f;
        glm::vec4 clip       = worldToShadowUV(ls, aheadPoint);
        REQUIRE(clip.w > 0.f); // in front of the eye
        glm::vec3 ndc = glm::vec3(clip) / clip.w;

        REQUIRE(ndc.x == Approx(0.f).margin(1e-3f));
        REQUIRE(ndc.y == Approx(0.f).margin(1e-3f));
        REQUIRE(ndc.z >= 0.f);
        REQUIRE(ndc.z <= 1.f);
    }
}

TEST_CASE("pointShadowFaceMatrix: adjacent faces disagree on a point straight ahead of one of them",
          "[shadow][point]") {
    // A point directly ahead of face 0 (+X) should project OUTSIDE the
    // [-1,1] clip range (or behind the eye) on face 2's (+Y) frustum, since
    // it's a 90-degree-FOV frustum pointed along a different axis. This is
    // the property that makes per-face textures actually distinguish which
    // face a fragment belongs to, rather than all reading the same map.
    glm::vec3 lightPos{0.f, 0.f, 0.f};
    constexpr float kNear = 0.1f;
    constexpr float kFar  = 20.f;

    glm::mat4 faceYLightSpace = pointShadowFaceMatrix(lightPos, 2, kNear, kFar);
    glm::vec3 pointAheadOfX   = lightPos + cubeFaceDirection(0) * 5.f; // (5,0,0)
    glm::vec4 clip            = worldToShadowUV(faceYLightSpace, pointAheadOfX);

    bool behindEye = clip.w <= 0.f;
    bool outsideXY = false;
    if (!behindEye) {
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        outsideXY     = std::abs(ndc.x) > 1.f || std::abs(ndc.y) > 1.f;
    }
    REQUIRE((behindEye || outsideXY));
}

TEST_CASE("pointShadowFaceMatrix is finite for every face", "[shadow][point]") {
    glm::vec3 lightPos{1.f, 2.f, 3.f};
    for (int face = 0; face < 6; ++face) {
        glm::mat4 ls = pointShadowFaceMatrix(lightPos, face, 0.1f, 25.f);
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                REQUIRE(std::isfinite(ls[col][row]));
            }
        }
    }
}
