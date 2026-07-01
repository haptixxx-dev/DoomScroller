#include "engine/ecs/EnemySystem.h"

#include "engine/PhysicsWorld.h"
#include "engine/ecs/Components.h"

#include <glm/glm.hpp>

namespace ds {

void enemySystem(entt::registry& world, PhysicsWorld& physics, ScriptSystem& scripts, glm::vec3 playerPos, float dt,
                 HealthComponent* playerHealth, float* playerIFrames, const EnemyProjectileFn& spawnProjectile) {
    auto view = world.view<EnemyComponent, Transform>();
    for (auto [entity, enemy, transform] : view.each()) {
        if (enemy.health <= 0) {
            world.destroy(entity);
            continue;
        }

        if (enemy.attackCooldown > 0.f)
            enemy.attackCooldown -= dt;

        glm::vec3 pos      = physics.getPosition(enemy.physicsBodyId);
        transform.position = pos;

        glm::vec3 toPlayer = playerPos - pos;
        toPlayer.y         = 0.f;
        float dist         = glm::length(toPlayer);
        glm::vec3 dir      = dist > 1e-4f ? toPlayer / dist : glm::vec3{0.f, 0.f, 1.f};
        float vy           = physics.getLinearVelocity(enemy.physicsBodyId).y;

        EnemyAIDecision decision =
            scripts.enemyAITick(static_cast<int>(enemy.archetype), static_cast<int>(enemy.state), dist,
                                 enemy.attackCooldown, enemy.moveSpeed, enemy.attackRange, enemy.detectionRange,
                                 enemy.attackInterval, enemy.chargeWindup, enemy.chargeSpeed);

        enemy.state = static_cast<EnemyComponent::State>(decision.state);

        if (decision.setVelocity) {
            float speed = decision.lunge ? enemy.chargeSpeed : enemy.moveSpeed;
            physics.setLinearVelocity(
                enemy.physicsBodyId,
                {dir.x * speed * decision.moveIntent, vy, dir.z * speed * decision.moveIntent});
        }

        if (decision.armWindup)
            enemy.attackCooldown = enemy.chargeWindup;

        if (decision.fireProjectile && spawnProjectile) {
            EnemyProjectileSpawn spawn{};
            // Muzzle a little in front of the enemy at roughly eye level.
            spawn.origin      = pos + dir * 0.6f + glm::vec3{0.f, 0.5f, 0.f};
            spawn.velocity    = dir * enemy.projectileSpeed;
            spawn.damage      = enemy.attackDamage;
            spawn.ownerBodyId = enemy.physicsBodyId;
            spawnProjectile(spawn);
            enemy.attackCooldown = enemy.attackInterval;
        }

        if (decision.lunge) {
            if (playerHealth && playerIFrames)
                applyDamage(*playerHealth, *playerIFrames, enemy.attackDamage, 0.5f);
            // Re-arm the next windup regardless of whether the hit landed.
            enemy.attackCooldown = enemy.chargeWindup;
        }

        if (decision.meleeAttack && playerHealth && playerIFrames) {
            if (applyDamage(*playerHealth, *playerIFrames, enemy.attackDamage, 0.5f))
                enemy.attackCooldown = enemy.attackInterval;
        }
    }
}

} // namespace ds
