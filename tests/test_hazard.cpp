#include "engine/ecs/Hazard.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace ds;

// Pure hazard proximity + damage-over-time tick math (Phase 4 Wave D). Links
// engine_math/engine_headers only: Hazard.h is header-only glm math with no
// SDL3/Jolt/EnTT state.

TEST_CASE("hazardHits is true inside the radius and false outside", "[hazard]") {
    const glm::vec3 center{5.f, 0.f, -3.f};
    const float radius = 2.f;

    // Dead center and on the surface both count (inclusive).
    REQUIRE(hazardHits(center, radius, center));
    REQUIRE(hazardHits(center, radius, center + glm::vec3{2.f, 0.f, 0.f}));  // exactly on the edge
    REQUIRE(hazardHits(center, radius, center + glm::vec3{0.f, 1.9f, 0.f})); // just inside

    // Just outside on each of a few directions.
    REQUIRE_FALSE(hazardHits(center, radius, center + glm::vec3{2.01f, 0.f, 0.f}));
    REQUIRE_FALSE(hazardHits(center, radius, center + glm::vec3{0.f, 0.f, 3.f}));
    // A diagonal point past the corner of the radius sphere.
    REQUIRE_FALSE(hazardHits(center, radius, center + glm::vec3{1.5f, 1.5f, 0.f})); // dist ~2.12 > 2

    // A non-positive radius never hits, even at the exact center.
    REQUIRE_FALSE(hazardHits(center, 0.f, center));
    REQUIRE_FALSE(hazardHits(center, -1.f, center));
}

TEST_CASE("hazardTick fires exactly once per interval and re-arms", "[hazard]") {
    constexpr float kInterval = 0.5f;
    float timer               = kInterval; // armed for the first tick at t = interval

    // Sub-interval steps do not fire until the accumulated time reaches interval.
    REQUIRE_FALSE(hazardTick(timer, 0.2f, kInterval)); // t = 0.2
    REQUIRE_FALSE(hazardTick(timer, 0.2f, kInterval)); // t = 0.4
    // Crossing the interval fires once and re-arms for the next one.
    REQUIRE(hazardTick(timer, 0.2f, kInterval)); // t = 0.6 >= 0.5 -> fire

    // After re-arm, the leftover phase (0.1s over) is preserved: only 0.4s more
    // is needed to reach the next tick, not a full interval.
    REQUIRE_FALSE(hazardTick(timer, 0.2f, kInterval)); // t = 0.8
    REQUIRE(hazardTick(timer, 0.2f, kInterval));       // t = 1.0 -> fire
}

TEST_CASE("hazardTick counts exactly one fire per whole interval over a big dt", "[hazard]") {
    constexpr float kInterval = 0.5f;
    float timer               = kInterval;

    // A single 1.6s step spans 3 whole intervals (ticks due at 0.5, 1.0, 1.5).
    // Apply the big dt once, then drain the remaining accumulated intervals with
    // zero-dt calls, counting every fire.
    int fires = 0;
    if (hazardTick(timer, 1.6f, kInterval))
        ++fires;
    while (hazardTick(timer, 0.f, kInterval))
        ++fires;

    REQUIRE(fires == 3);
    // The timer is left correctly re-armed for the next tick: it is > 0 and the
    // remaining time to the next fire is <= interval.
    REQUIRE(timer > 0.f);
    REQUIRE(timer <= Approx(kInterval).epsilon(1e-4f));
}

TEST_CASE("hazardTick with a non-positive interval fires once then stops", "[hazard]") {
    float timer = 0.f;
    // Degenerate cadence: a single fire, then no re-arm loop.
    REQUIRE(hazardTick(timer, 0.f, 0.f));
    REQUIRE_FALSE(hazardTick(timer, 0.f, 0.f));
    REQUIRE_FALSE(hazardTick(timer, 0.1f, 0.f));
}

TEST_CASE("HazardStats carries the tunable damage-over-time knobs", "[hazard]") {
    HazardStats s;
    // Sensible non-degenerate defaults.
    REQUIRE(s.radius > 0.f);
    REQUIRE(s.damagePerTick > 0);
    REQUIRE(s.tickInterval > 0.f);

    // Fields are independently settable (data-driven from Lua later).
    s.radius        = 4.5f;
    s.damagePerTick = 25;
    s.tickInterval  = 0.25f;
    REQUIRE(s.radius == Approx(4.5f));
    REQUIRE(s.damagePerTick == 25);
    REQUIRE(s.tickInterval == Approx(0.25f));
}
