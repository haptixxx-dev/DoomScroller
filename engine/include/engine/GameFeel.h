#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace ds {

// Pure (engine-independent) "game-feel" math: trauma-based screenshake,
// additive spring recoil, and hitstop. All deterministic — no rand() and no
// wall-clock; shake is a pure function of trauma plus a time value the caller
// passes. Kept free of SDL3 / Jolt / EnTT so it can be unit-tested against
// engine_headers only.

// ---------------------------------------------------------------------------
// Screenshake (trauma model, à la Squirrel Eiserloh's "Juice it or lose it").
//
// A single scalar `trauma` in [0,1] accumulates from hits/explosions and
// decays linearly over time. The visible shake is trauma^2 so small leftover
// trauma is barely felt while a full hit punches hard.
// ---------------------------------------------------------------------------
struct ScreenShake {
    float trauma = 0.f;
};

// Add trauma from an event, clamping the running total into [0,1]. Negative
// `amount` is allowed (clamps up from below to 0). Pure; mutates the shake.
inline void addTrauma(ScreenShake& shake, float amount) {
    shake.trauma = std::clamp(shake.trauma + amount, 0.f, 1.f);
}

// Bleed trauma off at `recoveryPerSec` per second, floored at zero. Pure.
inline void decayTrauma(ScreenShake& shake, float dt, float recoveryPerSec = 1.2f) {
    shake.trauma = std::max(0.f, shake.trauma - recoveryPerSec * dt);
}

// Deterministic 1-D value noise in [-1,1] from a continuous parameter. Uses the
// classic fract(sin(x)*c) hash; no state, no rand(). Same input -> same output.
inline float shakeNoise(float x) {
    float s = std::sin(x) * 43758.5453f;
    float f = s - std::floor(s); // fract -> [0,1)
    return f * 2.f - 1.f;        // -> [-1,1)
}

// Angular shake offsets (pitch=x, yaw=y, roll=z) in radians for the current
// `trauma` and `time`. Magnitude scales with trauma*trauma * maxAngle; each
// axis uses a distinct noise seed and a different time frequency so the axes
// move independently. trauma == 0 returns exactly vec3(0). Pure.
inline glm::vec3 shakeOffset(float trauma, float time, float maxAngle = 0.1f) {
    if (trauma <= 0.f)
        return glm::vec3(0.f);
    float magnitude = trauma * trauma * maxAngle;
    float pitch     = shakeNoise(time * 13.13f + 0.0f);
    float yaw       = shakeNoise(time * 17.17f + 100.0f);
    float roll      = shakeNoise(time * 23.23f + 200.0f);
    return glm::vec3(pitch, yaw, roll) * magnitude;
}

// ---------------------------------------------------------------------------
// Recoil: an additive look-offset (x=yaw, y=pitch, radians) that a weapon kicks
// and a critically-ish damped spring pulls back to zero. Layered on top of the
// player's aim rather than replacing it.
// ---------------------------------------------------------------------------
struct Recoil {
    glm::vec2 offset{0.f};   // current additive look offset (yaw, pitch)
    glm::vec2 velocity{0.f}; // spring velocity
};

// Apply an instantaneous recoil impulse (e.g. a weapon kick). Additive on the
// spring velocity so consecutive shots stack. Pure; mutates the recoil.
inline void addRecoil(Recoil& recoil, glm::vec2 kick) {
    recoil.velocity += kick;
}

// Advance the recoil spring one step. A spring with `stiffness` pulls the
// offset back to zero and `damping` bleeds the velocity, so the offset settles
// at zero. Semi-implicit Euler for stability. Pure; mutates the recoil.
inline void tickRecoil(Recoil& recoil, float dt, float stiffness = 60.f, float damping = 12.f) {
    glm::vec2 accel = -stiffness * recoil.offset - damping * recoil.velocity;
    recoil.velocity += accel * dt;
    recoil.offset += recoil.velocity * dt;
}

// ---------------------------------------------------------------------------
// Hitstop: a brief slow-down of game time on impact for crunchy feedback. The
// caller multiplies its sim dt by the returned scale.
// ---------------------------------------------------------------------------
struct Hitstop {
    float timer = 0.f; // remaining hitstop time (real seconds)
};

// Trigger a hitstop of at least `seconds`. Never shortens an active, longer
// hitstop (takes the max). Pure; mutates the hitstop.
inline void triggerHitstop(Hitstop& hitstop, float seconds) {
    hitstop.timer = std::max(hitstop.timer, seconds);
}

// Tick the hitstop by the *real* (unscaled) dt and return the dt-scale to apply
// to sim time this frame: `frozenScale` (a small value in (0,1]) while the
// timer is active, else 1.0. Pure; mutates the hitstop.
inline float tickHitstop(Hitstop& hitstop, float realDt, float frozenScale = 0.05f) {
    if (hitstop.timer > 0.f) {
        hitstop.timer = std::max(0.f, hitstop.timer - realDt);
        return frozenScale;
    }
    return 1.f;
}

} // namespace ds
