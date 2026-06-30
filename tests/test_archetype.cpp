#include "engine/ecs/Components.h"
#include "engine/ecs/EnemyArchetype.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

using namespace ds;

TEST_CASE("archetypeDefaults differ meaningfully per type", "[archetype]") {
    ArchetypeStats grunt   = archetypeDefaults(EnemyArchetype::Grunt);
    ArchetypeStats charger = archetypeDefaults(EnemyArchetype::Charger);
    ArchetypeStats ranged  = archetypeDefaults(EnemyArchetype::Ranged);

    // Charger is faster than the baseline Grunt.
    REQUIRE(charger.moveSpeed > grunt.moveSpeed);

    // Charger telegraphs a lunge and has a high burst speed; Grunt does not.
    REQUIRE(charger.chargeWindup > 0.f);
    REQUIRE(charger.chargeSpeed > charger.moveSpeed);
    REQUIRE(grunt.chargeWindup == 0.f);

    // Ranged fires projectiles; Grunt and Charger are melee (no projectiles).
    REQUIRE(ranged.projectileSpeed > 0.f);
    REQUIRE(grunt.projectileSpeed == 0.f);
    REQUIRE(charger.projectileSpeed == 0.f);

    // Ranged keeps its distance: longer attack reach than the melee types.
    REQUIRE(ranged.attackRange > grunt.attackRange);
    REQUIRE(ranged.attackRange > charger.attackRange);

    // Ranged is a glass cannon: lower health than the melee types.
    REQUIRE(ranged.health < grunt.health);
    REQUIRE(ranged.health < charger.health);
}

TEST_CASE("applyArchetype stamps stats onto an EnemyComponent", "[archetype]") {
    EnemyComponent e;
    ArchetypeStats charger = archetypeDefaults(EnemyArchetype::Charger);

    applyArchetype(e, EnemyArchetype::Charger);

    REQUIRE(e.archetype == EnemyArchetype::Charger);
    REQUIRE(e.moveSpeed == Catch::Approx(charger.moveSpeed));
    REQUIRE(e.health == charger.health);
    REQUIRE(e.attackRange == Catch::Approx(charger.attackRange));
    REQUIRE(e.attackDamage == charger.attackDamage);
    REQUIRE(e.detectionRange == Catch::Approx(charger.detectionRange));
    REQUIRE(e.attackInterval == Catch::Approx(charger.attackInterval));
    REQUIRE(e.chargeWindup == Catch::Approx(charger.chargeWindup));
    REQUIRE(e.chargeSpeed == Catch::Approx(charger.chargeSpeed));
    REQUIRE(e.projectileSpeed == Catch::Approx(charger.projectileSpeed));
}

TEST_CASE("applyArchetype Ranged enables projectiles and lowers health", "[archetype]") {
    EnemyComponent e; // default Grunt
    REQUIRE(e.projectileSpeed == 0.f);

    applyArchetype(e, EnemyArchetype::Ranged);

    REQUIRE(e.archetype == EnemyArchetype::Ranged);
    REQUIRE(e.projectileSpeed > 0.f);
    REQUIRE(e.health < 100);
}

TEST_CASE("applyArchetype leaves runtime fields untouched", "[archetype]") {
    EnemyComponent e;
    e.physicsBodyId  = 42;
    e.attackCooldown = 0.75f;
    e.state          = EnemyComponent::State::Chase;

    applyArchetype(e, EnemyArchetype::Charger);

    REQUIRE(e.physicsBodyId == 42);
    REQUIRE(e.attackCooldown == Catch::Approx(0.75f));
    REQUIRE(e.state == EnemyComponent::State::Chase);
}

TEST_CASE("EnemyComponent archetype defaults to Grunt", "[archetype]") {
    EnemyComponent e;
    REQUIRE(e.archetype == EnemyArchetype::Grunt);
    REQUIRE(e.chargeWindup == 0.f);
    REQUIRE(e.chargeSpeed == 0.f);
    REQUIRE(e.projectileSpeed == 0.f);
}
