#include "engine/Camera.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <glm/glm.hpp>

using Catch::Approx;

static bool vec3Near(glm::vec3 a, glm::vec3 b, float eps = 1e-3f) {
    return glm::length(a - b) < eps;
}

TEST_CASE("Camera default front looks down -Z", "[camera]") {
    ds::Camera cam;
    // yaw=-90 deg, pitch=0 => front = (0,0,-1)
    glm::vec3 f = cam.front();
    REQUIRE(vec3Near(f, {0.f, 0.f, -1.f}));
}

TEST_CASE("Camera yaw 0 degrees looks down +X", "[camera]") {
    ds::Camera cam;
    cam.yaw = 0.f;
    glm::vec3 f = cam.front();
    REQUIRE(vec3Near(f, {1.f, 0.f, 0.f}));
}

TEST_CASE("Camera yaw 90 degrees looks down +Z", "[camera]") {
    ds::Camera cam;
    cam.yaw = 90.f;  // front = (cos90, 0, sin90) = (0, 0, 1)
    glm::vec3 f = cam.front();
    REQUIRE(vec3Near(f, {0.f, 0.f, 1.f}));
}

TEST_CASE("Camera pitch 90 degrees looks straight up", "[camera]") {
    ds::Camera cam;
    cam.pitch = 89.f;  // clamped at 89 in rotate(), test near-up
    glm::vec3 f = cam.front();
    REQUIRE(f.y > 0.99f);
}

TEST_CASE("Camera right vector perpendicular to front and world up", "[camera]") {
    ds::Camera cam;
    glm::vec3 f = cam.front();
    glm::vec3 r = cam.right();
    REQUIRE(glm::abs(glm::dot(f, r)) < 1e-4f);
}

TEST_CASE("Camera viewMatrix moves scene by camera position", "[camera]") {
    ds::Camera cam;
    cam.position = {5.f, 0.f, 0.f};
    glm::mat4 v = cam.viewMatrix();
    // origin in world space => position -5 in view space
    glm::vec4 worldOrigin{0.f, 0.f, 0.f, 1.f};
    glm::vec4 inView = v * worldOrigin;
    REQUIRE(Approx(inView.x).margin(1e-3f) == -5.f);
    REQUIRE(Approx(inView.y).margin(1e-3f) == 0.f);
}

TEST_CASE("Camera projMatrix maps near clip to 0 in depth (GLM_FORCE_DEPTH_ZERO_TO_ONE)", "[camera]") {
    ds::Camera cam;
    glm::mat4 p = cam.projMatrix(16.f / 9.f);
    // Point exactly at nearZ along -Z in view space => clip depth = 0
    // In view space: (0, 0, -nearZ, 1)
    glm::vec4 nearPoint{0.f, 0.f, -cam.nearZ, 1.f};
    glm::vec4 clip = p * nearPoint;
    float ndcZ = clip.z / clip.w;
    REQUIRE(Approx(ndcZ).margin(1e-3f) == 0.f);
}

TEST_CASE("Camera projMatrix maps far clip to 1 in depth", "[camera]") {
    ds::Camera cam;
    glm::mat4 p = cam.projMatrix(16.f / 9.f);
    glm::vec4 farPoint{0.f, 0.f, -cam.farZ, 1.f};
    glm::vec4 clip = p * farPoint;
    float ndcZ = clip.z / clip.w;
    REQUIRE(Approx(ndcZ).margin(1e-3f) == 1.f);
}
