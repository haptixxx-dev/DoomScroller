#pragma once

#include <algorithm>
#include <glm/glm.hpp>

namespace ds {

// Pure (engine-independent) parry + dash-cancel timing math. A parry is a short
// active window (opened on input, gated by a cooldown) during which incoming
// damage is negated and a Ranged projectile can be reflected back at its owner.
// Kept free of SDL3 / Jolt / EnTT so it can be unit-tested against
// engine_headers only. ParryTuning could fold into MovementTuning if parry is
// owned by PlayerController.

// Tunables for the parry window. windowDuration is how long the parry stays
// active after triggering; cooldown is the min time between parries; dashRefund
// is how many dash charges a successful parry grants back (0 = none).
struct ParryTuning {
    float windowDuration = 0.3f;
    float cooldown       = 0.6f;
    float dashRefund     = 1.f;
};

// Per-player parry timers. `window` counts down the active parry; `cooldown`
// counts down the lockout before another parry can start. active() is true
// while the window is open (this frame's incoming damage is negated).
struct ParryState {
    float window   = 0.f;
    float cooldown = 0.f;

    bool active() const { return window > 0.f; }
};

// Start a parry: only allowed when off cooldown. Opens the window for
// windowDuration and arms the cooldown for tuning.cooldown. A trigger while
// still cooling down is ignored (no-op). Pure; mutates state.
inline void triggerParry(ParryState& state, const ParryTuning& tuning) {
    if (state.cooldown > 0.f)
        return;
    state.window   = tuning.windowDuration;
    state.cooldown = tuning.cooldown;
}

// Advance the parry timers by dt, decaying both window and cooldown toward 0
// (clamped). Call once per frame. Pure; mutates state.
inline void tickParry(ParryState& state, float dt) {
    state.window   = std::max(0.f, state.window - dt);
    state.cooldown = std::max(0.f, state.cooldown - dt);
}

// True while the parry window is active, meaning incoming damage this frame is
// negated. Pure.
inline bool parrySucceeds(const ParryState& state) {
    return state.active();
}

// Compute the velocity for a reflected projectile: flip the incoming velocity
// and scale its magnitude by speedBoost (default 1.5x faster on the way back).
// The engine also swaps the projectile's ownerBodyId (shooter -> player) so the
// reflected bolt can hit enemies instead of being ignored by its own owner ray.
// Pure.
inline glm::vec3 reflectProjectileVelocity(const glm::vec3& incoming, float speedBoost = 1.5f) {
    return -incoming * speedBoost;
}

// True if the player is still in a recovery window (recoveryTimer > 0), in
// which case a dash is allowed to interrupt (cancel) that recovery. Pure.
inline bool canDashCancel(float recoveryTimer) {
    return recoveryTimer > 0.f;
}

} // namespace ds
