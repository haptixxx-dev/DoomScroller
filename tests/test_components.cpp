#include "engine/ecs/Components.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <glm/glm.hpp>

using namespace ds;

TEST_CASE("EnemyComponent default values", "[components]") {
    EnemyComponent e;
    REQUIRE(e.health == 100);
    REQUIRE(e.state == EnemyComponent::State::Idle);
    REQUIRE(e.detectionRange == 15.f);
    REQUIRE(e.attackRange == 1.5f);
    REQUIRE(e.moveSpeed == 3.f);
}

TEST_CASE("WeaponComponent default values", "[components]") {
    WeaponComponent w;
    REQUIRE(w.damage == 25);
    REQUIRE(w.fireRate == 5.f);
    REQUIRE(w.cooldown == 0.f);
    REQUIRE(w.firedThisFrame == false);
}

TEST_CASE("Transform default is identity", "[components]") {
    Transform t{};
    REQUIRE(t.position == glm::vec3(0.f));
    REQUIRE(t.scale == glm::vec3(1.f));
    REQUIRE(t.rotation == glm::quat(1.f, 0.f, 0.f, 0.f));
    REQUIRE(t.modelMatrix() == glm::mat4(1.f));
}

TEST_CASE("EnemyComponent state transitions are distinct enum values", "[components]") {
    using S = EnemyComponent::State;
    REQUIRE(S::Idle != S::Chase);
    REQUIRE(S::Chase != S::Attack);
    REQUIRE(S::Idle != S::Attack);
}

TEST_CASE("WeaponComponent cooldown tracks fire rate correctly", "[components]") {
    WeaponComponent w;
    // 5 shots/sec => period = 0.2s
    float period = 1.f / w.fireRate;
    REQUIRE(period == Catch::Approx(0.2f));

    // Simulate fire: set cooldown to period, tick it down
    w.cooldown = period;
    float dt = 0.1f;
    w.cooldown -= dt;
    REQUIRE(w.cooldown == Catch::Approx(0.1f));
    REQUIRE(w.cooldown > 0.f);  // still on cooldown

    w.cooldown -= dt;
    REQUIRE(w.cooldown == Catch::Approx(0.f).margin(1e-6f));  // ready to fire
}

TEST_CASE("SpawnPoint default position is origin", "[components]") {
    SpawnPoint sp;
    REQUIRE(sp.position == glm::vec3(0.f));
}
