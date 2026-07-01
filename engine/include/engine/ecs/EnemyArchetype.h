#pragma once

#include "engine/ecs/Components.h"

#include <cstdint>

namespace ds {

// Data-driven stat block for an enemy archetype. archetypeDefaults() returns
// the tuned values per EnemyArchetype; applyArchetype() stamps them onto an
// EnemyComponent (also recording the archetype tag so EnemySystem can branch).
// Pure / header-only: depends only on Components.h + the C++ stdlib so it links
// into the math/logic test target with no SDL3/Jolt/EnTT runtime state.
struct ArchetypeStats {
    float moveSpeed;
    float attackRange;
    int attackDamage;
    float detectionRange;
    int health;
    float attackInterval;
    float chargeWindup;    // > 0 only for telegraphed-lunge archetypes (Charger)
    float chargeSpeed;     // lunge burst speed (Charger)
    float projectileSpeed; // > 0 only for projectile-firing archetypes (Ranged)
};

// Tuned per-archetype defaults.
//   Grunt   - melee chaser matching the legacy EnemyComponent defaults.
//   Charger - faster baseline move, telegraphed lunge (chargeWindup > 0,
//             high chargeSpeed), tankier, longer reach to start the lunge.
//   Ranged  - slower, keeps distance, fires projectiles (projectileSpeed > 0)
//             from a long attackRange, lower health (glass cannon).
//   Brute   - very slow, very tanky melee bruiser: huge health, low move speed,
//             heavy hits on a long swing interval (no charge, no projectile).
//   Spitter - fast, fragile burst-ranged: low health, short attack interval so
//             it fires rapid volleys (projectileSpeed > 0).
inline ArchetypeStats archetypeDefaults(EnemyArchetype type) {
    switch (type) {
    case EnemyArchetype::Charger:
        return ArchetypeStats{
            /*moveSpeed*/ 5.f,
            /*attackRange*/ 2.5f,
            /*attackDamage*/ 20,
            /*detectionRange*/ 18.f,
            /*health*/ 140,
            /*attackInterval*/ 1.5f,
            /*chargeWindup*/ 0.6f,
            /*chargeSpeed*/ 16.f,
            /*projectileSpeed*/ 0.f,
        };
    case EnemyArchetype::Ranged:
        return ArchetypeStats{
            /*moveSpeed*/ 2.f,
            /*attackRange*/ 12.f,
            /*attackDamage*/ 8,
            /*detectionRange*/ 22.f,
            /*health*/ 60,
            /*attackInterval*/ 1.2f,
            /*chargeWindup*/ 0.f,
            /*chargeSpeed*/ 0.f,
            /*projectileSpeed*/ 18.f,
        };
    case EnemyArchetype::Brute:
        return ArchetypeStats{
            /*moveSpeed*/ 2.f,
            /*attackRange*/ 2.f,
            /*attackDamage*/ 30,
            /*detectionRange*/ 16.f,
            /*health*/ 300,
            /*attackInterval*/ 1.8f,
            /*chargeWindup*/ 0.f,
            /*chargeSpeed*/ 0.f,
            /*projectileSpeed*/ 0.f,
        };
    case EnemyArchetype::Spitter:
        return ArchetypeStats{
            /*moveSpeed*/ 4.f,
            /*attackRange*/ 11.f,
            /*attackDamage*/ 6,
            /*detectionRange*/ 22.f,
            /*health*/ 45,
            /*attackInterval*/ 0.6f,
            /*chargeWindup*/ 0.f,
            /*chargeSpeed*/ 0.f,
            /*projectileSpeed*/ 22.f,
        };
    case EnemyArchetype::Grunt:
    default:
        return ArchetypeStats{
            /*moveSpeed*/ 3.f,
            /*attackRange*/ 1.5f,
            /*attackDamage*/ 10,
            /*detectionRange*/ 15.f,
            /*health*/ 100,
            /*attackInterval*/ 1.f,
            /*chargeWindup*/ 0.f,
            /*chargeSpeed*/ 0.f,
            /*projectileSpeed*/ 0.f,
        };
    }
}

// Stamp an archetype's stat block onto an enemy, leaving runtime fields
// (state, physicsBodyId, attackCooldown) untouched so it is safe to call at
// spawn time after the body has been created.
inline void applyArchetype(EnemyComponent& enemy, EnemyArchetype type) {
    const ArchetypeStats stats = archetypeDefaults(type);
    enemy.archetype            = type;
    enemy.moveSpeed            = stats.moveSpeed;
    enemy.attackRange          = stats.attackRange;
    enemy.attackDamage         = stats.attackDamage;
    enemy.detectionRange       = stats.detectionRange;
    enemy.health               = stats.health;
    enemy.attackInterval       = stats.attackInterval;
    enemy.chargeWindup         = stats.chargeWindup;
    enemy.chargeSpeed          = stats.chargeSpeed;
    enemy.projectileSpeed      = stats.projectileSpeed;
}

} // namespace ds
