#pragma once

#include <entt/entt.hpp>
#include <functional>
#include <glm/glm.hpp>

namespace ds {
class PhysicsWorld;

// Result of a projectile detonation, passed back to the engine so it can spawn
// VFX/audio/light without ProjectileSystem depending on those subsystems.
struct ProjectileImpact {
    glm::vec3 position{0.f};    // world-space detonation point
    glm::vec3 normal{0.f};      // surface/travel normal (points back toward the shooter)
    float splashRadius = 0.f;
    bool hitEnemy      = false; // true if a direct enemy hit (vs. world geometry / timeout)
};

// Called once per detonation. Optional; may be empty.
using ProjectileImpactFn = std::function<void(const ProjectileImpact&)>;

// Splash damage with linear falloff: full `baseDamage` at the blast center,
// dropping to zero at `radius`. Returns 0 when out of range or radius <= 0.
// Kept pure (header-inline) so it is unit-testable without the full engine.
inline int splashDamage(int baseDamage, float distance, float radius) {
    if (radius <= 0.f || distance >= radius || baseDamage <= 0)
        return 0;
    float falloff = 1.f - (distance / radius);
    int dmg       = static_cast<int>(static_cast<float>(baseDamage) * falloff);
    return dmg < 0 ? 0 : dmg;
}

// Advances every entity carrying a ProjectileComponent:
//   - integrates motion (pos += vel * dt) and ages its lifetime,
//   - ray-casts along this frame's movement to detect a hit (enemy or world),
//   - on a direct hit applies full damage to the struck enemy and, when
//     splashRadius > 0, radial splash to other enemies with linear falloff,
//   - destroys the projectile on hit, lifetime expiry, or leaving the world.
//
// onImpact (if set) fires for each detonation so the caller can spawn an
// explosion + transient light. Damage is applied to EnemyComponent::health;
// death is resolved by enemySystem the following frame.
void projectileSystem(entt::registry& world, PhysicsWorld& physics, float dt, const ProjectileImpactFn& onImpact = {});

} // namespace ds
