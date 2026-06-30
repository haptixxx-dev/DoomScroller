#include "engine/Tonemap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

using namespace ds;

TEST_CASE("black maps to black for all operators", "[tonemap]") {
    glm::vec3 zero(0.f);
    REQUIRE(reinhard(zero).x == Catch::Approx(0.f));
    REQUIRE(reinhardExtended(zero, 4.f).x == Catch::Approx(0.f));
    REQUIRE(acesFilmic(zero).x == Catch::Approx(0.f));

    REQUIRE(tonemap(zero, 1.f, TonemapOp::Reinhard).y == Catch::Approx(0.f));
    REQUIRE(tonemap(zero, 1.f, TonemapOp::ReinhardExtended).y == Catch::Approx(0.f));
    REQUIRE(tonemap(zero, 1.f, TonemapOp::ACES).y == Catch::Approx(0.f));
}

TEST_CASE("reinhard(1) is 0.5 per channel", "[tonemap]") {
    glm::vec3 r = reinhard(glm::vec3(1.f));
    REQUIRE(r.x == Catch::Approx(0.5f));
    REQUIRE(r.y == Catch::Approx(0.5f));
    REQUIRE(r.z == Catch::Approx(0.5f));
}

TEST_CASE("reinhard is monotonic per channel", "[tonemap]") {
    glm::vec3 a = reinhard(glm::vec3(1.f));
    glm::vec3 b = reinhard(glm::vec3(2.f));
    REQUIRE(b.x > a.x);
    REQUIRE(b.y > a.y);
    REQUIRE(b.z > a.z);
}

TEST_CASE("applyExposure scales linearly", "[tonemap]") {
    glm::vec3 c(0.2f, 0.4f, 0.8f);
    glm::vec3 scaled = applyExposure(c, 2.f);
    REQUIRE(scaled.x == Catch::Approx(0.4f));
    REQUIRE(scaled.y == Catch::Approx(0.8f));
    REQUIRE(scaled.z == Catch::Approx(1.6f));

    glm::vec3 identity = applyExposure(c, 1.f);
    REQUIRE(identity.z == Catch::Approx(0.8f));
}

TEST_CASE("ACES clamps large values to <=1 and keeps black at 0", "[tonemap]") {
    glm::vec3 big = acesFilmic(glm::vec3(1000.f));
    REQUIRE(big.x <= 1.f);
    REQUIRE(big.y <= 1.f);
    REQUIRE(big.z <= 1.f);
    REQUIRE(big.x == Catch::Approx(1.f));

    glm::vec3 black = acesFilmic(glm::vec3(0.f));
    REQUIRE(black.x == Catch::Approx(0.f));
}

TEST_CASE("ACES is monotonic over increasing inputs", "[tonemap]") {
    float prev = -1.f;
    for (float v : {0.1f, 0.3f, 0.6f, 1.0f, 1.5f}) {
        float mapped = acesFilmic(glm::vec3(v)).x;
        REQUIRE(mapped > prev);
        prev = mapped;
    }
}

TEST_CASE("ACES and extended Reinhard pin their exact constants", "[tonemap]") {
    // ACES (Narkowicz fit) at input 1.0: (1*(2.51+0.03))/(1*(2.43+0.59)+0.14)
    // = 2.54/3.16 ~= 0.8038. A corrupted a/b/d/e/f constant shifts this.
    REQUIRE(acesFilmic(glm::vec3(1.0f)).x == Catch::Approx(0.8021f).epsilon(0.01f));

    // Extended Reinhard at c=1, whitePoint=4: (1*(1 + 1/16))/(1 + 1) = 0.53125.
    REQUIRE(reinhardExtended(glm::vec3(1.f), 4.f).x == Catch::Approx(0.53125f).epsilon(1e-4f));

    // The white point maps to exactly 1.0: (4*(1 + 16/16))/(1 + 4) = 5/5 = 1.0.
    REQUIRE(reinhardExtended(glm::vec3(4.f), 4.f).x == Catch::Approx(1.0f).epsilon(1e-4f));
}

TEST_CASE("tonemap output is always within [0,1]", "[tonemap]") {
    const TonemapOp ops[] = {TonemapOp::Reinhard, TonemapOp::ReinhardExtended, TonemapOp::ACES};
    for (TonemapOp op : ops) {
        for (float v : {0.f, 0.5f, 1.f, 5.f, 50.f, 500.f}) {
            glm::vec3 out = tonemap(glm::vec3(v), 1.5f, op, 4.f);
            REQUIRE(out.x >= 0.f);
            REQUIRE(out.x <= 1.f);
            REQUIRE(out.y >= 0.f);
            REQUIRE(out.y <= 1.f);
            REQUIRE(out.z >= 0.f);
            REQUIRE(out.z <= 1.f);
        }
    }
}
