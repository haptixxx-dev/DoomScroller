#include "engine/ecs/EnemySystem.h"

#include "engine/PhysicsWorld.h"
#include "engine/ecs/Components.h"

#include <glm/glm.hpp>

namespace ds {

void enemySystem(entt::registry& world, PhysicsWorld& physics, glm::vec3 playerPos, float dt,
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

        const bool ranged  = enemy.projectileSpeed > 0.f;
        const bool charger = enemy.chargeWindup > 0.f;

        switch (enemy.state) {
        case EnemyComponent::State::Idle:
            if (dist < enemy.detectionRange)
                enemy.state = EnemyComponent::State::Chase;
            physics.setLinearVelocity(enemy.physicsBodyId, {0.f, vy, 0.f});
            break;

        case EnemyComponent::State::Chase:
            if (dist < enemy.attackRange) {
                enemy.state = EnemyComponent::State::Attack;
                // Charger: arm the lunge windup as it enters attack range.
                if (charger)
                    enemy.attackCooldown = enemy.chargeWindup;
                physics.setLinearVelocity(enemy.physicsBodyId, {0.f, vy, 0.f});
            } else if (dist > enemy.detectionRange) {
                enemy.state = EnemyComponent::State::Idle;
            } else {
                physics.setLinearVelocity(enemy.physicsBodyId, {dir.x * enemy.moveSpeed, vy, dir.z * enemy.moveSpeed});
            }
            break;

        case EnemyComponent::State::Attack:
            if (ranged) {
                // Ranged: hold position at range, retreat if the player closes
                // inside half the attack range, and fire on cooldown.
                if (dist > enemy.attackRange) {
                    enemy.state = EnemyComponent::State::Chase;
                    break;
                }
                if (dist < enemy.attackRange * 0.5f)
                    physics.setLinearVelocity(enemy.physicsBodyId,
                                              {-dir.x * enemy.moveSpeed, vy, -dir.z * enemy.moveSpeed});
                else
                    physics.setLinearVelocity(enemy.physicsBodyId, {0.f, vy, 0.f});

                if (enemy.attackCooldown <= 0.f && spawnProjectile) {
                    EnemyProjectileSpawn spawn{};
                    // Muzzle a little in front of the enemy at roughly eye level.
                    spawn.origin      = pos + dir * 0.6f + glm::vec3{0.f, 0.5f, 0.f};
                    spawn.velocity    = dir * enemy.projectileSpeed;
                    spawn.damage      = enemy.attackDamage;
                    spawn.ownerBodyId = enemy.physicsBodyId;
                    spawnProjectile(spawn);
                    enemy.attackCooldown = enemy.attackInterval;
                }
            } else if (charger) {
                // Charger: count down the windup (telegraph) holding still, then
                // burst toward the player at chargeSpeed. Re-arm after the lunge.
                if (dist > enemy.attackRange) {
                    enemy.state = EnemyComponent::State::Chase;
                    break;
                }
                if (enemy.attackCooldown > 0.f) {
                    // Telegraph: freeze horizontal movement during the windup.
                    physics.setLinearVelocity(enemy.physicsBodyId, {0.f, vy, 0.f});
                } else {
                    // Lunge: burst toward the player and apply contact damage.
                    physics.setLinearVelocity(enemy.physicsBodyId,
                                              {dir.x * enemy.chargeSpeed, vy, dir.z * enemy.chargeSpeed});
                    if (playerHealth && playerIFrames)
                        applyDamage(*playerHealth, *playerIFrames, enemy.attackDamage, 0.5f);
                    // Re-arm the next windup regardless of whether the hit landed.
                    enemy.attackCooldown = enemy.chargeWindup;
                }
            } else {
                // Grunt: melee contact damage on the attack cooldown.
                if (dist > enemy.attackRange) {
                    enemy.state = EnemyComponent::State::Chase;
                } else if (enemy.attackCooldown <= 0.f && playerHealth && playerIFrames) {
                    if (applyDamage(*playerHealth, *playerIFrames, enemy.attackDamage, 0.5f))
                        enemy.attackCooldown = enemy.attackInterval;
                }
                physics.setLinearVelocity(enemy.physicsBodyId, {0.f, vy, 0.f});
            }
            break;
        }
    }
}

} // namespace ds
