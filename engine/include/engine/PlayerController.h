#pragma once

#include "engine/Camera.h"
#include "engine/PhysicsWorld.h"

#include <glm/glm.hpp>

namespace ds {

class PlayerController {
  public:
    float moveSpeed = 5.f;
    float jumpSpeed = 6.f;

    PlayerController(PhysicsWorld& physics, uint32_t bodyId);

    // Call each frame. moveDir: XZ in world space (already projected, not normalized).
    // jump: true on the frame the jump key is pressed.
    void update(Camera& camera, glm::vec3 moveDir, bool jump, float dt);

    glm::vec3 eyePosition() const;

  private:
    static constexpr float kHalfHeight  = 0.5f;
    static constexpr float kRadius      = 0.4f;
    static constexpr float kEyeOffset   = 0.8f; // above capsule center
    static constexpr float kCoyoteWindow = 0.12f;
    static constexpr float kGroundCheckDist = 0.15f;

    PhysicsWorld& m_physics;
    uint32_t m_bodyId;
    float m_coyoteTimer = 0.f;
};

} // namespace ds
