#pragma once

#include <glm/glm.hpp>

namespace ds {

// =============================================================================
// Environmental hazards (Phase 4 Wave D, greenfield). Pure proximity + tick math
// so the damage-over-time logic lands + tests here before any component wiring
// or rendering. A hazard is a static world volume (lava pool, spike floor, gas
// cloud) that ticks damage to anything inside its radius on a fixed cadence.
//
// Kept header-only + glm-only (no RHI, no EnTT, no SDL3) so it links into the
// pure math/logic test target. The eventual HazardComponent + hazard system
// (mirroring the pickup system) will hold a HazardStats + a per-hazard timer and
// call these helpers each frame.
// =============================================================================

// Tunable stats for one hazard volume. radius is the world-space influence
// sphere; damagePerTick is applied to each target inside it every time the tick
// timer fires; tickInterval is the cadence in seconds between damage ticks.
struct HazardStats {
    float radius       = 2.f;
    int damagePerTick  = 10;
    float tickInterval = 0.5f;
};

// True if `targetPos` is within `radius` of `hazardPos` (inclusive). Uses the
// squared distance to avoid the sqrt. A non-positive radius never hits. Pure.
inline bool hazardHits(const glm::vec3& hazardPos, float radius, const glm::vec3& targetPos) {
    if (radius <= 0.f)
        return false;
    const glm::vec3 d = targetPos - hazardPos;
    return glm::dot(d, d) <= radius * radius;
}

// Advance a hazard's cooldown timer by `dt` and report whether a damage tick
// fires this call. Cooldown-gated: subtract dt from `timer`; when it reaches or
// passes zero, re-arm it by adding `interval` and return true — otherwise return
// false. Re-arming by ADD (rather than resetting to interval) means the phase is
// preserved, so a large dt that overshoots one interval leaves the leftover on
// the clock; calling again with dt == 0 drains each remaining whole interval,
// firing once per interval. A non-positive interval is treated as a single
// immediate fire that then disables further ticks (timer parked at a large
// value) to avoid an infinite re-arm loop. Pure aside from mutating `timer`.
inline bool hazardTick(float& timer, float dt, float interval) {
    timer -= dt;
    if (timer > 0.f)
        return false;

    if (interval <= 0.f) {
        // Degenerate cadence: fire once, then park the timer so we never re-fire.
        timer = 1.f;
        return true;
    }
    timer += interval;
    return true;
}

} // namespace ds
