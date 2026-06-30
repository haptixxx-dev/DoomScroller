#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include "engine/Frustum.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using Catch::Approx;
using namespace ds;

// View-projection for a camera at (0,0,5) looking toward the origin (down -Z),
// 70-degree vertical FOV, square aspect, [0.1, 100] depth range.
static glm::mat4 makeViewProj() {
    glm::mat4 proj = glm::perspective(glm::radians(70.f), 1.0f, 0.1f, 100.f);
    glm::mat4 view = glm::lookAt(glm::vec3{0.f, 0.f, 5.f}, glm::vec3{0.f, 0.f, 0.f}, glm::vec3{0.f, 1.f, 0.f});
    return proj * view;
}

TEST_CASE("aabbFromCenterExtents builds symmetric box", "[frustum]") {
    AABB box = aabbFromCenterExtents({1.f, 2.f, 3.f}, {0.5f, 0.5f, 0.5f});
    REQUIRE(box.min.x == Approx(0.5f));
    REQUIRE(box.max.z == Approx(3.5f));
}

TEST_CASE("extracted frustum planes are normalized", "[frustum]") {
    Frustum f = extractFrustum(makeViewProj());
    for (const Plane& plane : f.planes) {
        REQUIRE(glm::length(plane.normal) == Approx(1.f).margin(1e-4f));
    }
}

TEST_CASE("AABB at origin is inside the frustum", "[frustum]") {
    Frustum f   = extractFrustum(makeViewProj());
    AABB inside = aabbFromCenterExtents({0.f, 0.f, 0.f}, {1.f, 1.f, 1.f});
    REQUIRE(aabbInFrustum(f, inside));
}

TEST_CASE("AABB far behind the camera is culled", "[frustum]") {
    Frustum f   = extractFrustum(makeViewProj());
    AABB behind = aabbFromCenterExtents({0.f, 0.f, 50.f}, {1.f, 1.f, 1.f});
    REQUIRE_FALSE(aabbInFrustum(f, behind));
}

TEST_CASE("AABB far off to the side is culled", "[frustum]") {
    Frustum f   = extractFrustum(makeViewProj());
    AABB beside = aabbFromCenterExtents({1000.f, 0.f, 0.f}, {1.f, 1.f, 1.f});
    REQUIRE_FALSE(aabbInFrustum(f, beside));
}

TEST_CASE("near plane uses [0,1] clip convention, not OpenGL", "[frustum]") {
    // The whole reason this header exists: under GLM_FORCE_DEPTH_ZERO_TO_ONE the
    // near plane is row2 alone (world z = 4.90 here: camera z=5, near=0.1). The
    // OpenGL [-1,1] derivation (near = row3 + row2) would place it at z = 4.95.
    // A thin sliver wedged at z in [4.92, 4.93] is BEHIND the correct near plane
    // (so: culled) yet IN FRONT of the buggy OpenGL one (kept). This is the only
    // test that fails if the convention regresses — the broad in/out tests pass
    // under either derivation.
    Frustum f   = extractFrustum(makeViewProj());
    AABB sliver = aabbFromCenterExtents({0.f, 0.f, 4.925f}, {0.01f, 0.01f, 0.005f});
    REQUIRE_FALSE(aabbInFrustum(f, sliver));
}

TEST_CASE("Plane signedDistance sign follows half-space convention", "[frustum]") {
    Plane p{{0.f, 1.f, 0.f}, 0.f}; // y >= 0 is inside
    REQUIRE(p.signedDistance({0.f, 2.f, 0.f}) > 0.f);
    REQUIRE(p.signedDistance({0.f, -2.f, 0.f}) < 0.f);
}

TEST_CASE("Plane normalize scales normal to unit length", "[frustum]") {
    Plane p{{0.f, 0.f, 4.f}, 8.f};
    p.normalize();
    REQUIRE(glm::length(p.normal) == Approx(1.f));
    REQUIRE(p.normal.z == Approx(1.f));
    REQUIRE(p.d == Approx(2.f));
}
