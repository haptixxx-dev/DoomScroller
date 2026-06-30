#pragma once

#include "engine/ScriptSystem.h"

#include <cstdint>
#include <entt/entt.hpp>
#include <functional>
#include <glm/glm.hpp>

namespace ds {
class PhysicsWorld;
struct HealthComponent;

// Request to spawn a Ranged enemy's projectile, handed back to the Engine so it
// can attach a visible mesh/material alongside the ProjectileComponent (the AI
// system has no device/material access). origin is the muzzle world position,
// velocity is already aimed and scaled, damage is the enemy's attack damage, and
// ownerBodyId is the firing enemy's physics body (ignored by the hit ray).
struct EnemyProjectileSpawn {
    glm::vec3 origin{0.f};
    glm::vec3 velocity{0.f};
    int damage           = 0;
    uint32_t ownerBodyId = 0;
};
using EnemyProjectileFn = std::function<void(const EnemyProjectileSpawn&)>;

// Drives enemy AI and melee contact damage. The FSM decision (state
// transitions, attack/lunge/fire timing) lives in Lua (assets/scripts/
// enemy_ai.lua, module ds.enemy_ai, via scripts.enemyAITick); this function
// keeps the EnTT/Jolt loop (position sync, dist/dir computation, the
// health<=0 destroy check) and applies the returned decision (velocity,
// damage, projectile spawn — all C++-only state the Lua side can't see).
// When an enemy is in the Attack state and within attackRange, melee archetypes
// deal attackDamage to playerHealth on a per-enemy cooldown. Damage is gated by
// playerIFrames (shared i-frame timer); playerHealth/playerIFrames may be null
// to disable player damage. spawnProjectile may be empty (disables Ranged fire).
void enemySystem(entt::registry& world, PhysicsWorld& physics, ScriptSystem& scripts, glm::vec3 playerPos, float dt,
                 HealthComponent* playerHealth = nullptr, float* playerIFrames = nullptr,
                 const EnemyProjectileFn& spawnProjectile = {});

} // namespace ds
