#include "engine/PickupSystem.h"
#include "engine/ecs/Components.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

using namespace ds;

TEST_CASE("withinPickupRange collects only inside the sphere", "[pickup]") {
    glm::vec3 player{0.f, 1.f, 0.f};
    REQUIRE(withinPickupRange(player, {0.f, 1.f, 0.f}, 1.5f));       // coincident
    REQUIRE(withinPickupRange(player, {1.f, 1.f, 0.f}, 1.5f));       // inside
    REQUIRE_FALSE(withinPickupRange(player, {3.f, 1.f, 0.f}, 1.5f)); // outside
}

TEST_CASE("withinPickupRange respects the full 3D distance", "[pickup]") {
    glm::vec3 player{0.f, 0.f, 0.f};
    // 2-2-1 => length 3, just outside radius 2.9, inside radius 3.1.
    REQUIRE_FALSE(withinPickupRange(player, {2.f, 2.f, 1.f}, 2.9f));
    REQUIRE(withinPickupRange(player, {2.f, 2.f, 1.f}, 3.1f));
}

TEST_CASE("withinPickupRange never collects with non-positive radius", "[pickup]") {
    glm::vec3 p{0.f, 0.f, 0.f};
    REQUIRE_FALSE(withinPickupRange(p, p, 0.f));
    REQUIRE_FALSE(withinPickupRange(p, p, -1.f));
}

TEST_CASE("pickupEffectMagnitude clamps to available headroom", "[pickup]") {
    REQUIRE(pickupEffectMagnitude(25, 100) == 25); // full value fits
    REQUIRE(pickupEffectMagnitude(25, 10) == 10);  // clamped to headroom
    REQUIRE(pickupEffectMagnitude(25, 0) == 0);    // no headroom -> nothing
}

TEST_CASE("pickupEffectMagnitude never returns negatives", "[pickup]") {
    REQUIRE(pickupEffectMagnitude(-5, 100) == 0);
    REQUIRE(pickupEffectMagnitude(25, -5) == 0);
}

TEST_CASE("PickupComponent defaults", "[pickup]") {
    PickupComponent p;
    REQUIRE(p.kind == PickupComponent::Kind::Health);
    REQUIRE(p.value == 25);
}

TEST_CASE("health pickup heals up to the missing amount", "[pickup]") {
    HealthComponent h{40, 100};
    int grant = pickupEffectMagnitude(25, h.max - h.current);
    h.current += grant;
    REQUIRE(h.current == 65);

    // A second big pickup cannot over-heal past max.
    int grant2 = pickupEffectMagnitude(100, h.max - h.current);
    h.current += grant2;
    REQUIRE(h.current == 100);
}
