#include "engine/ecs/ProjectileSystem.h"

#include "engine/PhysicsWorld.h"
#include "engine/ecs/Components.h"

#include <glm/glm.hpp>
#include <vector>

namespace ds {

void projectileSystem(entt::registry& world, PhysicsWorld& physics, float dt, const ProjectileImpactFn& onImpact) {
    // Collect detonations first; deferring entity destruction keeps the view
    // iteration valid and lets splash damage read a stable enemy set.
    std::vector<entt::entity> toDestroy;

    auto view = world.view<Transform, ProjectileComponent>();
    for (auto [entity, transform, proj] : view.each()) {
        proj.lifetime -= dt;

        glm::vec3 start = transform.position;
        glm::vec3 step  = proj.velocity * dt;
        float dist      = glm::length(step);

        bool detonate = false;
        ProjectileImpact impact{};
        impact.splashRadius = proj.splashRadius;

        // Sweep this frame's travel for a hit. Guard against zero-length steps
        // (a stationary projectile still ages out via lifetime).
        if (dist > 1e-5f) {
            glm::vec3 dir = step / dist;
            glm::vec3 hitPoint{0.f};
            uint32_t hitId = physics.castRay(start, dir, dist, proj.ownerBodyId, hitPoint);
            if (hitId != UINT32_MAX) {
                detonate            = true;
                impact.position     = hitPoint;
                impact.normal       = -dir;
                impact.hitBodyId    = hitId;
                impact.ownerBodyId  = proj.ownerBodyId;
                impact.directDamage = proj.damage;

                // Direct hit: find the enemy bound to the struck body, if any.
                auto enemies = world.view<EnemyComponent, Transform>();
                for (auto [e, enemy, et] : enemies.each()) {
                    if (enemy.physicsBodyId == hitId) {
                        enemy.health -= proj.damage;
                        impact.hitEnemy = true;
                        impact.enemyHits.push_back(ProjectileEnemyHit{et.position, proj.damage, enemy.health <= 0});
                        break;
                    }
                }

                // Radial splash with linear falloff (skips the direct-hit body
                // so it isn't double-counted).
                if (proj.splashRadius > 0.f) {
                    auto splashView = world.view<EnemyComponent, Transform>();
                    for (auto [e, enemy, et] : splashView.each()) {
                        if (enemy.physicsBodyId == hitId)
                            continue;
                        float d   = glm::length(et.position - impact.position);
                        int extra = splashDamage(proj.damage, d, proj.splashRadius);
                        if (extra > 0) {
                            enemy.health -= extra;
                            impact.enemyHits.push_back(ProjectileEnemyHit{et.position, extra, enemy.health <= 0});
                        }
                    }
                }
            }
        }

        // Integrate motion for the surviving travel.
        transform.position = start + step;

        if (!detonate && proj.lifetime <= 0.f) {
            detonate        = true;
            impact.position = transform.position;
            impact.normal   = dist > 1e-5f ? -(step / dist) : glm::vec3{0.f, 1.f, 0.f};
        }

        if (detonate) {
            if (onImpact)
                onImpact(impact);
            toDestroy.push_back(entity);
        }
    }

    for (entt::entity e : toDestroy)
        if (world.valid(e))
            world.destroy(e);
}

} // namespace ds
