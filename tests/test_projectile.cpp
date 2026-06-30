#include "engine/ecs/Components.h"
#include "engine/ecs/ProjectileSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ds;

TEST_CASE("ProjectileComponent default values", "[projectile]") {
    ProjectileComponent p;
    REQUIRE(p.velocity == glm::vec3(0.f));
    REQUIRE(p.damage == 50);
    REQUIRE(p.lifetime == Catch::Approx(5.f));
    REQUIRE(p.splashRadius == 0.f);
    REQUIRE(p.ownerBodyId == 0u);
}

TEST_CASE("WeaponComponent gains projectile fields with sane defaults", "[projectile]") {
    WeaponComponent w;
    REQUIRE(w.type == WeaponType::Hitscan);
    REQUIRE(w.projectileSpeed == Catch::Approx(30.f));
    REQUIRE(w.projectileLifetime == Catch::Approx(5.f));
    REQUIRE(w.splashRadius == 0.f);
    REQUIRE(w.ammo == -1);
}

TEST_CASE("splashDamage is full at the center and zero at the edge", "[projectile]") {
    REQUIRE(splashDamage(80, 0.f, 4.f) == 80);
    REQUIRE(splashDamage(80, 4.f, 4.f) == 0); // exactly at radius -> out of range
    REQUIRE(splashDamage(80, 5.f, 4.f) == 0); // beyond radius
}

TEST_CASE("splashDamage falls off linearly with distance", "[projectile]") {
    // Halfway out: ~50% of base damage.
    REQUIRE(splashDamage(80, 2.f, 4.f) == 40);
    // Quarter out: ~75%.
    REQUIRE(splashDamage(80, 1.f, 4.f) == 60);
    // Three-quarters out: ~25%.
    REQUIRE(splashDamage(80, 3.f, 4.f) == 20);
}

TEST_CASE("splashDamage handles degenerate inputs", "[projectile]") {
    REQUIRE(splashDamage(80, 1.f, 0.f) == 0);  // no splash radius
    REQUIRE(splashDamage(80, 1.f, -1.f) == 0); // negative radius
    REQUIRE(splashDamage(0, 0.f, 4.f) == 0);   // no base damage
    REQUIRE(splashDamage(-10, 0.f, 4.f) == 0); // negative base damage
}
