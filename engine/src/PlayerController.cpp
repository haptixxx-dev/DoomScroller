#include "engine/PlayerController.h"

#include "engine/MovementTech.h"

#include <algorithm>
#include <glm/glm.hpp>

namespace ds {

PlayerController::PlayerController(PhysicsWorld& physics, uint32_t bodyId)
    : m_physics(physics), m_bodyId(bodyId), m_dashCharges(tuning.dashMaxCharges) {
    // Start with a full dash bank.
}

void PlayerController::update(Camera& camera, glm::vec3 moveDir, bool jump, bool dashPressed, bool crouchHeld,
                              float dt) {
    m_dashedThisFrame       = false;
    m_slideStartedThisFrame = false;

    // --- Timers --------------------------------------------------------------
    if (m_iFrames > 0.f)
        m_iFrames = std::max(0.f, m_iFrames - dt);
    if (m_dashCooldown > 0.f)
        m_dashCooldown = std::max(0.f, m_dashCooldown - dt);
    regenDashCharges(m_dashCharges, m_dashRegenTimer, dt, tuning.dashMaxCharges, tuning.dashRegenTime);

    // --- Ground check --------------------------------------------------------
    glm::vec3 center = m_physics.getPosition(m_bodyId);
    glm::vec3 bottom = center + glm::vec3{0.f, -(kHalfHeight + kRadius - 0.05f), 0.f};
    bool grounded    = m_physics.castRayDown(bottom, kGroundCheckDist, m_bodyId);

    if (grounded)
        m_coyoteTimer = kCoyoteWindow;
    else
        m_coyoteTimer = std::max(0.f, m_coyoteTimer - dt);

    bool canJump = m_coyoteTimer > 0.f;

    glm::vec3 vel      = m_physics.getLinearVelocity(m_bodyId);
    glm::vec3 horizVel = {vel.x, 0.f, vel.z};
    float horizSpeed   = glm::length(horizVel);
    bool hasInput      = glm::length(moveDir) > 0.f;
    glm::vec3 wishDir  = hasInput ? glm::normalize(moveDir) : glm::vec3{0.f};

    // --- Dash trigger --------------------------------------------------------
    // Dash in the input direction, or straight ahead (camera-forward proxy via
    // current facing) when no movement key is held. Costs one charge.
    if (dashPressed && m_dashTimer <= 0.f && m_dashCooldown <= 0.f && m_dashCharges > 0) {
        glm::vec3 dir = hasInput ? wishDir : (horizSpeed > 0.01f ? glm::normalize(horizVel) : glm::vec3{0.f});
        if (glm::length(dir) > 0.f) {
            m_dashDir      = dir;
            m_dashTimer    = tuning.dashDuration;
            m_dashCooldown = tuning.dashCooldown;
            m_iFrames      = std::max(m_iFrames, tuning.dashIFrames);
            --m_dashCharges;
            m_dashedThisFrame = true;
        }
    }

    // --- Slide trigger / maintenance ----------------------------------------
    if (!m_sliding) {
        // Enter a slide: crouch while grounded and moving fast enough.
        if (crouchHeld && grounded && horizSpeed >= tuning.slideEnterSpeed && m_dashTimer <= 0.f) {
            m_sliding               = true;
            m_slideTimer            = 0.f;
            m_slideStartedThisFrame = true;
            // One-time momentum boost along the current travel direction.
            glm::vec3 slideDir = horizSpeed > 0.01f ? glm::normalize(horizVel) : wishDir;
            float boosted      = horizSpeed * tuning.slideBoost;
            horizVel           = slideDir * boosted;
            horizSpeed         = boosted;
        }
    } else {
        m_slideTimer += dt;
        // Friction bleeds speed; exit when slow, key released, airborne or timed out.
        horizSpeed         = applyFriction(horizSpeed, tuning.slideFriction, dt);
        glm::vec3 slideDir = glm::length(horizVel) > 0.01f ? glm::normalize(horizVel) : wishDir;
        horizVel           = slideDir * horizSpeed;
        if (!crouchHeld || !grounded || horizSpeed < tuning.slideMinSpeed || m_slideTimer >= tuning.slideMaxTime)
            m_sliding = false;
    }

    // --- Horizontal velocity model ------------------------------------------
    if (m_dashTimer > 0.f) {
        // Dash overrides the accel model: hold a large constant burst velocity.
        m_dashTimer -= dt;
        horizVel = m_dashDir * tuning.dashSpeed;
    } else if (m_sliding) {
        // Slide already wrote horizVel above (momentum + friction); leave it.
    } else {
        // ULTRAKILL-snappy accel model: drive toward the desired velocity using
        // accel when there is input, decel when there is none, with separate
        // ground/air rates.
        glm::vec3 target = wishDir * tuning.moveSpeed;
        float rate;
        if (hasInput)
            rate = grounded ? tuning.groundAccel : tuning.airAccel;
        else
            rate = grounded ? tuning.groundDecel : tuning.airDecel;
        horizVel = approachVelocity(horizVel, target, rate, dt);
    }

    vel.x = horizVel.x;
    vel.z = horizVel.z;

    if (jump && canJump) {
        vel.y         = tuning.jumpSpeed;
        m_coyoteTimer = 0.f;
        m_sliding     = false; // jumping cancels a slide
    }

    m_physics.setLinearVelocity(m_bodyId, vel);

    // Sync camera to eye position (mouse look is already set via rotate())
    camera.position = eyePosition();
}

glm::vec3 PlayerController::eyePosition() const {
    glm::vec3 center = m_physics.getPosition(m_bodyId);
    float eye        = kEyeOffset - (m_sliding ? tuning.slideEyeDrop : 0.f);
    return center + glm::vec3{0.f, eye, 0.f};
}

} // namespace ds
