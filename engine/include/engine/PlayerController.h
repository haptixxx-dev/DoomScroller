#pragma once

#include "engine/Camera.h"
#include "engine/MovementTech.h"
#include "engine/PhysicsWorld.h"

#include <algorithm>
#include <glm/glm.hpp>

namespace ds {

class PlayerController {
  public:
    // Movement tunables (accel model + dash/slide). Public so the engine can
    // tweak them and tests can inspect defaults.
    MovementTuning tuning;

    PlayerController(PhysicsWorld& physics, uint32_t bodyId);

    // Call each frame. moveDir: XZ in world space (already projected, not normalized).
    // jump: true on the frame the jump key is pressed.
    // dashPressed: true on the frame a dash is requested (Shift / double-tap).
    // crouchHeld: true while the crouch/slide key is held (Ctrl).
    void update(Camera& camera, glm::vec3 moveDir, bool jump, bool dashPressed, bool crouchHeld, float dt);

    glm::vec3 eyePosition() const;

    // Restore up to `count` dash charges (clamped to the configured max). Used
    // by the parry's dash-refund reward (task 35).
    void refundDash(int count) { m_dashCharges = std::min(tuning.dashMaxCharges, m_dashCharges + count); }

    // --- Movement tech state (read-only accessors for HUD / hooks) ---------
    int dashCharges() const { return m_dashCharges; }
    bool dashedThisFrame() const { return m_dashedThisFrame; }
    bool slidStartedThisFrame() const { return m_slideStartedThisFrame; }
    bool sliding() const { return m_sliding; }
    // Remaining invulnerability granted by a dash (seconds). The engine folds
    // this into its damage gate (task 11 i-frames).
    float iFrames() const { return m_iFrames; }

  private:
    static constexpr float kHalfHeight      = 0.5f;
    static constexpr float kRadius          = 0.4f;
    static constexpr float kEyeOffset       = 0.8f; // above capsule center
    static constexpr float kCoyoteWindow    = 0.12f;
    static constexpr float kGroundCheckDist = 0.15f;

    PhysicsWorld& m_physics;
    uint32_t m_bodyId;
    float m_coyoteTimer = 0.f;

    // Dash state.
    int m_dashCharges      = 0;
    float m_dashRegenTimer = 0.f;
    float m_dashTimer      = 0.f; // remaining burst duration; >0 means dashing
    float m_dashCooldown   = 0.f;
    glm::vec3 m_dashDir{0.f};

    // Slide state.
    bool m_sliding     = false;
    float m_slideTimer = 0.f;

    // I-frames granted by dashing.
    float m_iFrames = 0.f;

    // One-frame event flags for SFX/VFX hooks.
    bool m_dashedThisFrame       = false;
    bool m_slideStartedThisFrame = false;
};

} // namespace ds
