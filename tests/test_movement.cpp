#include "engine/MovementTech.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

using namespace ds;

TEST_CASE("MovementTuning sane defaults", "[movement]") {
    MovementTuning t;
    REQUIRE(t.dashMaxCharges == 2);
    REQUIRE(t.groundAccel > t.airAccel); // ground is snappier than air
    REQUIRE(t.dashSpeed > t.moveSpeed);  // dash is a burst above run speed
    REQUIRE(t.slideBoost > 1.f);
}

TEST_CASE("regenDashCharges restores one charge per regenTime", "[movement]") {
    int charges = 0;
    float timer = 0.f;
    // 1.0s regen; advance 0.4s thrice -> 1.2s total -> exactly one charge.
    regenDashCharges(charges, timer, 0.4f, 2, 1.0f);
    REQUIRE(charges == 0);
    regenDashCharges(charges, timer, 0.4f, 2, 1.0f);
    REQUIRE(charges == 0);
    regenDashCharges(charges, timer, 0.4f, 2, 1.0f);
    REQUIRE(charges == 1);
    REQUIRE(timer == Catch::Approx(0.2f)); // 1.2 - 1.0 carried over
}

TEST_CASE("regenDashCharges can restore multiple charges in one big step", "[movement]") {
    int charges = 0;
    float timer = 0.f;
    regenDashCharges(charges, timer, 5.5f, 2, 2.0f); // 5.5s, 2s each -> 2 charges, cap
    REQUIRE(charges == 2);
    REQUIRE(timer == Catch::Approx(0.f));            // capped: timer reset
}

TEST_CASE("regenDashCharges does not regen past max", "[movement]") {
    int charges = 2;
    float timer = 1.9f;
    regenDashCharges(charges, timer, 5.0f, 2, 2.0f);
    REQUIRE(charges == 2);
    REQUIRE(timer == Catch::Approx(0.f));
}

TEST_CASE("approachVelocity steps toward target without overshoot", "[movement]") {
    glm::vec3 cur{0.f};
    glm::vec3 tgt{10.f, 0.f, 0.f};
    // rate 100, dt 0.05 => max step 5; one step lands halfway.
    glm::vec3 a = approachVelocity(cur, tgt, 100.f, 0.05f);
    REQUIRE(a.x == Catch::Approx(5.f));
    // Next step covers remaining distance (5 <= maxStep) -> snaps to target.
    glm::vec3 b = approachVelocity(a, tgt, 100.f, 0.05f);
    REQUIRE(b.x == Catch::Approx(10.f));
}

TEST_CASE("approachVelocity snaps when within one step", "[movement]") {
    glm::vec3 cur{9.9f, 0.f, 0.f};
    glm::vec3 tgt{10.f, 0.f, 0.f};
    glm::vec3 a = approachVelocity(cur, tgt, 100.f, 0.05f); // maxStep 5 >> 0.1
    REQUIRE(a.x == Catch::Approx(10.f));
}

TEST_CASE("applyFriction bleeds speed and clamps at zero", "[movement]") {
    REQUIRE(applyFriction(10.f, 6.f, 0.5f) == Catch::Approx(7.f)); // 10 - 3
    REQUIRE(applyFriction(2.f, 6.f, 0.5f) == Catch::Approx(0.f));  // clamps
}

TEST_CASE("dash charge regen accumulates fractional dt across many frames", "[movement]") {
    int charges     = 0;
    float timer     = 0.f;
    const float dt  = 1.f / 60.f;
    const float reg = 1.0f;
    // 61 frames (~1.017s) crosses the 1.0s threshold -> one charge. (Exactly 60
    // frames sums to just under 1.0 in float, so we step past it.)
    for (int i = 0; i < 61; ++i)
        regenDashCharges(charges, timer, dt, 2, reg);
    REQUIRE(charges == 1);
}
