#pragma once

#include <algorithm>
#include <glm/glm.hpp>

namespace ds {

// Pure (engine-independent) movement-tech math: dash-charge regen and the
// accel-based velocity model used by PlayerController. Kept free of SDL3 /
// Jolt / EnTT so it can be unit-tested against engine_headers only.

// Tunables for the ULTRAKILL-style accel model and dash/slide tech. Held as a
// plain struct so PlayerController can expose it and tests can construct it.
struct MovementTuning {
    // Target horizontal ground speed (m/s).
    float moveSpeed = 7.f;
    float jumpSpeed = 7.f;

    // Acceleration toward the desired velocity. Ground is snappy; air is
    // capped so you commit to a jump but still steer a little. Decel applies
    // when there is no input (quick stop on ground, gentle drift in air).
    float groundAccel = 90.f;
    float airAccel    = 18.f;
    float groundDecel = 70.f;
    float airDecel    = 4.f;

    // Dash: instant velocity burst held for dashDuration, then control returns
    // to the accel model. Grants i-frames for the burst window.
    float dashSpeed     = 28.f;
    float dashDuration  = 0.12f;
    float dashIFrames   = 0.20f;
    int dashMaxCharges  = 2;
    float dashRegenTime = 2.5f;  // seconds to regenerate one charge
    float dashCooldown  = 0.25f; // min time between dashes

    // Slide: entered by crouching while moving fast enough. Boosts current
    // speed once, then friction bleeds it back toward walking speed. The
    // capsule is not physically resized (Jolt resize is heavy); instead the
    // eye is lowered by slideEyeDrop and a speed model carries momentum.
    float slideBoost      = 1.4f;  // multiplier applied to entry speed
    float slideFriction   = 6.f;   // m/s^2 bled off while sliding
    float slideMinSpeed   = 3.f;   // exit slide below this speed
    float slideMaxTime    = 1.2f;  // hard cap on slide duration
    float slideEnterSpeed = 6.f;   // min horizontal speed to start a slide
    float slideEyeDrop    = 0.45f; // how far the eye lowers while sliding
};

// Regenerate dash charges over time. `timer` accumulates dt; each time it
// crosses regenTime a charge is restored (up to maxCharges) and the timer
// wraps. No regen happens while already at max. Pure; mutates charges/timer.
inline void regenDashCharges(int& charges, float& timer, float dt, int maxCharges, float regenTime) {
    if (charges >= maxCharges) {
        timer = 0.f;
        return;
    }
    timer += dt;
    while (timer >= regenTime && charges < maxCharges) {
        timer -= regenTime;
        ++charges;
    }
    if (charges >= maxCharges)
        timer = 0.f;
}

// Move `current` toward `target` by at most accel*dt (component-wise vector
// version of a critically-damped approach). Used for both accel and decel by
// passing the matching rate. Pure.
inline glm::vec3 approachVelocity(const glm::vec3& current, const glm::vec3& target, float rate, float dt) {
    glm::vec3 delta = target - current;
    float dist      = glm::length(delta);
    float maxStep   = rate * dt;
    if (dist <= maxStep || dist <= 1e-6f)
        return target;
    return current + delta * (maxStep / dist);
}

// Apply scalar friction to a speed: bleeds `friction*dt` off, clamped at zero.
inline float applyFriction(float speed, float friction, float dt) {
    return std::max(0.f, speed - friction * dt);
}

} // namespace ds
