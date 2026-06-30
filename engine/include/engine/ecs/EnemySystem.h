#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace ds {
class PhysicsWorld;
struct HealthComponent;

// Drives enemy AI and melee contact damage. When an enemy is in the Attack
// state and within attackRange, it deals attackDamage to playerHealth on a
// per-enemy cooldown. Damage is gated by playerIFrames (shared i-frame timer);
// playerHealth/playerIFrames may be null to disable player damage.
void enemySystem(entt::registry& world, PhysicsWorld& physics, glm::vec3 playerPos, float dt,
                 HealthComponent* playerHealth = nullptr, float* playerIFrames = nullptr);

} // namespace ds
