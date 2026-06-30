#include "engine/ecs/Components.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using Catch::Approx;
using namespace ds;

static bool vec3Near(glm::vec3 a, glm::vec3 b, float eps = 1e-4f) {
    return glm::length(a - b) < eps;
}

static bool vec4Near(glm::vec4 a, glm::vec4 b, float eps = 1e-4f) {
    return glm::length(a - b) < eps;
}

TEST_CASE("Transform identity model matrix", "[transform]") {
    Transform t{};
    glm::mat4 m = t.modelMatrix();
    REQUIRE(m == glm::mat4(1.f));
}

TEST_CASE("Transform translation", "[transform]") {
    Transform t{};
    t.position  = {3.f, -1.f, 2.f};
    glm::mat4 m = t.modelMatrix();

    glm::vec4 origin{0.f, 0.f, 0.f, 1.f};
    glm::vec4 result = m * origin;
    REQUIRE(vec4Near(result, {3.f, -1.f, 2.f, 1.f}));
}

TEST_CASE("Transform scale", "[transform]") {
    Transform t{};
    t.scale     = {2.f, 3.f, 0.5f};
    glm::mat4 m = t.modelMatrix();

    glm::vec4 p{1.f, 1.f, 1.f, 1.f};
    glm::vec4 result = m * p;
    REQUIRE(vec4Near(result, {2.f, 3.f, 0.5f, 1.f}));
}

TEST_CASE("Transform 90-degree rotation around Y", "[transform]") {
    Transform t{};
    t.rotation  = glm::angleAxis(glm::radians(90.f), glm::vec3{0.f, 1.f, 0.f});
    glm::mat4 m = t.modelMatrix();

    // Right-handed GLM: R_y(90°) maps +Z -> +X
    glm::vec4 fwd{0.f, 0.f, 1.f, 0.f};
    glm::vec4 result = m * fwd;
    REQUIRE(vec4Near(result, {1.f, 0.f, 0.f, 0.f}));
}

TEST_CASE("Transform TRS order: scale then rotate then translate", "[transform]") {
    Transform t{};
    t.position  = {10.f, 0.f, 0.f};
    t.scale     = {2.f, 2.f, 2.f};
    glm::mat4 m = t.modelMatrix();

    // Unit point at origin should end up at (10,0,0) after translate
    glm::vec4 result = m * glm::vec4(0.f, 0.f, 0.f, 1.f);
    REQUIRE(vec4Near(result, {10.f, 0.f, 0.f, 1.f}));

    // Unit point at (1,0,0) should scale to (2,0,0) then translate to (12,0,0)
    result = m * glm::vec4(1.f, 0.f, 0.f, 1.f);
    REQUIRE(vec4Near(result, {12.f, 0.f, 0.f, 1.f}));
}
