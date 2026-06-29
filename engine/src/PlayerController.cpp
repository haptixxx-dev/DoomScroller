#include "engine/PlayerController.h"

#include <glm/glm.hpp>
#include <algorithm>

namespace ds {

PlayerController::PlayerController(PhysicsWorld& physics, uint32_t bodyId)
    : m_physics(physics), m_bodyId(bodyId) {}

void PlayerController::update(Camera& camera, glm::vec3 moveDir, bool jump, float dt) {
    // Ground check: cast ray from just above capsule bottom
    glm::vec3 center = m_physics.getPosition(m_bodyId);
    glm::vec3 bottom = center + glm::vec3{0.f, -(kHalfHeight + kRadius - 0.05f), 0.f};
    bool grounded    = m_physics.castRayDown(bottom, kGroundCheckDist, m_bodyId);

    if (grounded)
        m_coyoteTimer = kCoyoteWindow;
    else
        m_coyoteTimer = std::max(0.f, m_coyoteTimer - dt);

    bool canJump = m_coyoteTimer > 0.f;

    glm::vec3 vel = m_physics.getLinearVelocity(m_bodyId);

    // Horizontal: override XZ, preserve Y (gravity)
    vel.x = moveDir.x * moveSpeed;
    vel.z = moveDir.z * moveSpeed;

    if (jump && canJump) {
        vel.y       = jumpSpeed;
        m_coyoteTimer = 0.f;
    }

    m_physics.setLinearVelocity(m_bodyId, vel);

    // Sync camera to eye position (mouse look is already set via rotate())
    camera.position = eyePosition();
}

glm::vec3 PlayerController::eyePosition() const {
    glm::vec3 center = m_physics.getPosition(m_bodyId);
    return center + glm::vec3{0.f, kEyeOffset, 0.f};
}

} // namespace ds
