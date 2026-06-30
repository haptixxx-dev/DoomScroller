#pragma once

#include <glm/glm.hpp>

namespace ds {

// Pure pickup helpers (task 33). Kept free of any engine/SDL/Jolt state so the
// range check + effect magnitude are unit-testable in isolation. The Engine
// owns the pickup entities and applies the returned magnitudes to the player.

// True when the player is within `radius` units of a pickup (sphere test on the
// full 3D distance). Negative or zero radius never collects.
inline bool withinPickupRange(const glm::vec3& player, const glm::vec3& pickup, float radius) {
    if (radius <= 0.f)
        return false;
    glm::vec3 d = player - pickup;
    return glm::dot(d, d) <= radius * radius;
}

// Effect magnitude a pickup of the given face value grants, clamped so a pickup
// never over-heals past the missing amount (Health) or hands back a negative
// value. `value` is the pickup's authored amount; `headroom` is how much the
// target can still absorb (e.g. max-current health, or remaining dash charge
// slots). Returns the amount to actually apply: min(value, headroom), never < 0.
inline int pickupEffectMagnitude(int value, int headroom) {
    int v = value < 0 ? 0 : value;
    int h = headroom < 0 ? 0 : headroom;
    return v < h ? v : h;
}

} // namespace ds
