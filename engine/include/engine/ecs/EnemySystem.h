#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace ds {
class PhysicsWorld;

void enemySystem(entt::registry& world, PhysicsWorld& physics, glm::vec3 playerPos, float dt);

} // namespace ds
