#include "engine/ecs/Components.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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
    float dt   = 0.1f;
    w.cooldown -= dt;
    REQUIRE(w.cooldown == Catch::Approx(0.1f));
    REQUIRE(w.cooldown > 0.f); // still on cooldown

    w.cooldown -= dt;
    REQUIRE(w.cooldown == Catch::Approx(0.f).margin(1e-6f)); // ready to fire
}

TEST_CASE("SpawnPoint default position is origin and archetypeHint is unset", "[components]") {
    SpawnPoint sp;
    REQUIRE(sp.position == glm::vec3(0.f));
    REQUIRE(sp.archetypeHint == -1);
}

TEST_CASE("HealthComponent default values", "[components]") {
    HealthComponent h;
    REQUIRE(h.current == 100);
    REQUIRE(h.max == 100);
    REQUIRE(h.alive());
}

TEST_CASE("applyDamage reduces health and arms i-frames", "[components]") {
    HealthComponent h{100, 100};
    float iframes = 0.f;
    bool hit      = applyDamage(h, iframes, 30, 0.5f);
    REQUIRE(hit);
    REQUIRE(h.current == 70);
    REQUIRE(iframes == Catch::Approx(0.5f));
}

TEST_CASE("applyDamage is gated by active i-frames", "[components]") {
    HealthComponent h{100, 100};
    float iframes = 0.4f; // still invulnerable
    bool hit      = applyDamage(h, iframes, 30, 0.5f);
    REQUIRE_FALSE(hit);
    REQUIRE(h.current == 100);               // unchanged
    REQUIRE(iframes == Catch::Approx(0.4f)); // not re-armed
}

TEST_CASE("applyDamage clamps health at zero and reports death", "[components]") {
    HealthComponent h{20, 100};
    float iframes = 0.f;
    bool hit      = applyDamage(h, iframes, 50, 0.5f);
    REQUIRE(hit);
    REQUIRE(h.current == 0);
    REQUIRE_FALSE(h.alive());
}

TEST_CASE("applyDamage ignores non-positive damage", "[components]") {
    HealthComponent h{100, 100};
    float iframes = 0.f;
    REQUIRE_FALSE(applyDamage(h, iframes, 0, 0.5f));
    REQUIRE_FALSE(applyDamage(h, iframes, -5, 0.5f));
    REQUIRE(h.current == 100);
    REQUIRE(iframes == 0.f);
}

TEST_CASE("i-frame timer decrements toward zero over time", "[components]") {
    HealthComponent h{100, 100};
    float iframes = 0.f;
    applyDamage(h, iframes, 10, 0.5f);
    REQUIRE(iframes == Catch::Approx(0.5f));

    // Simulate frames at 0.1s; a second hit must be ignored until i-frames lapse.
    float dt = 0.1f;
    for (int i = 0; i < 4; ++i) {
        iframes -= dt;
        REQUIRE_FALSE(applyDamage(h, iframes, 10, 0.5f));
    }
    REQUIRE(iframes == Catch::Approx(0.1f));
    iframes -= 0.2f; // now clearly lapsed (negative)
    REQUIRE(iframes <= 0.f);
    REQUIRE(applyDamage(h, iframes, 10, 0.5f));
    REQUIRE(h.current == 80); // initial hit (-10) + this hit (-10)
}

TEST_CASE("EnemyComponent attack cadence defaults", "[components]") {
    EnemyComponent e;
    REQUIRE(e.attackCooldown == 0.f);
    REQUIRE(e.attackInterval == 1.f);
    REQUIRE(e.attackDamage == 10);
}
