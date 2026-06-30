#include "engine/GltfExtract.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <stdexcept>
#include <string>

using namespace ds;

namespace {
std::string fixturePath(const char* name) {
    return std::string(DS_FIXTURES_DIR) + "/" + name;
}
} // namespace

// tests/fixtures/triangle.gltf: one node ("TriangleNode", translation
// (2,3,4), identity rotation/scale) referencing one mesh with one triangle
// primitive: positions (0,0,0),(1,0,0),(0,1,0), indices 0,1,2.

TEST_CASE("extractTrianglePrimitives reads the fixture's geometry", "[gltf]") {
    auto prims = gltf::extractTrianglePrimitives(fixturePath("triangle.gltf"));

    REQUIRE(prims.size() == 1);
    REQUIRE(prims[0].vertices.size() == 3);
    REQUIRE(prims[0].indices.size() == 3);

    REQUIRE(prims[0].vertices[0].pos.x == Catch::Approx(0.f));
    REQUIRE(prims[0].vertices[1].pos.x == Catch::Approx(1.f));
    REQUIRE(prims[0].vertices[2].pos.y == Catch::Approx(1.f));

    REQUIRE(prims[0].indices[0] == 0u);
    REQUIRE(prims[0].indices[1] == 1u);
    REQUIRE(prims[0].indices[2] == 2u);
}

TEST_CASE("extractNodePrimitives carries the owning node's world transform", "[gltf]") {
    auto prims = gltf::extractNodePrimitives(fixturePath("triangle.gltf"));

    REQUIRE(prims.size() == 1);
    REQUIRE(prims[0].worldPosition.x == Catch::Approx(2.f));
    REQUIRE(prims[0].worldPosition.y == Catch::Approx(3.f));
    REQUIRE(prims[0].worldPosition.z == Catch::Approx(4.f));

    // Identity rotation (no rotation authored on the node).
    REQUIRE(prims[0].worldRotation.x == Catch::Approx(0.f).margin(1e-5));
    REQUIRE(prims[0].worldRotation.y == Catch::Approx(0.f).margin(1e-5));
    REQUIRE(prims[0].worldRotation.z == Catch::Approx(0.f).margin(1e-5));
    REQUIRE(std::abs(prims[0].worldRotation.w) == Catch::Approx(1.f));

    REQUIRE(prims[0].primitive.vertices.size() == 3);
    REQUIRE(prims[0].primitive.indices.size() == 3);
    // No scale authored (defaults to 1) -> vertex positions unchanged from
    // local space.
    REQUIRE(prims[0].primitive.vertices[1].pos.x == Catch::Approx(1.f));
}

TEST_CASE("extractTrianglePrimitives throws on a missing file", "[gltf]") {
    REQUIRE_THROWS_AS(gltf::extractTrianglePrimitives(fixturePath("does_not_exist.gltf")), std::runtime_error);
}

TEST_CASE("extractNodePrimitives throws on a missing file", "[gltf]") {
    REQUIRE_THROWS_AS(gltf::extractNodePrimitives(fixturePath("does_not_exist.gltf")), std::runtime_error);
}
