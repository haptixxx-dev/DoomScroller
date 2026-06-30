#include "engine/ecs/EnemySystem.h"

#include "engine/PhysicsWorld.h"
#include "engine/ecs/Components.h"

#include <glm/glm.hpp>

namespace ds {

void enemySystem(entt::registry& world, PhysicsWorld& physics, glm::vec3 playerPos, float dt,
                 HealthComponent* playerHealth, float* playerIFrames) {
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

        switch (enemy.state) {
        case EnemyComponent::State::Idle:
            if (dist < enemy.detectionRange)
                enemy.state = EnemyComponent::State::Chase;
            physics.setLinearVelocity(enemy.physicsBodyId,
                                      {0.f, physics.getLinearVelocity(enemy.physicsBodyId).y, 0.f});
            break;

        case EnemyComponent::State::Chase:
            if (dist < enemy.attackRange) {
                enemy.state = EnemyComponent::State::Attack;
                physics.setLinearVelocity(enemy.physicsBodyId,
                                          {0.f, physics.getLinearVelocity(enemy.physicsBodyId).y, 0.f});
            } else if (dist > enemy.detectionRange) {
                enemy.state = EnemyComponent::State::Idle;
            } else {
                glm::vec3 dir = glm::normalize(toPlayer);
                float vy      = physics.getLinearVelocity(enemy.physicsBodyId).y;
                physics.setLinearVelocity(enemy.physicsBodyId, {dir.x * enemy.moveSpeed, vy, dir.z * enemy.moveSpeed});
            }
            break;

        case EnemyComponent::State::Attack:
            if (dist > enemy.attackRange) {
                enemy.state = EnemyComponent::State::Chase;
            } else if (enemy.attackCooldown <= 0.f && playerHealth && playerIFrames) {
                if (applyDamage(*playerHealth, *playerIFrames, enemy.attackDamage, 0.5f))
                    enemy.attackCooldown = enemy.attackInterval;
            }
            physics.setLinearVelocity(enemy.physicsBodyId,
                                      {0.f, physics.getLinearVelocity(enemy.physicsBodyId).y, 0.f});
            break;
        }
    }
}

} // namespace ds
