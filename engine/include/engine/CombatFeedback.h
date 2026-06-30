#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace ds {

// Pure (engine-independent) combat-feedback support: floating damage numbers,
// crosshair hit markers, and a world-to-screen projection used to place the
// numbers in UI space. Kept free of SDL3 / Jolt / EnTT so it can be unit-tested
// against engine_headers only. The Engine layer owns the containers and feeds
// these into UISystem each frame.

// A floating damage number spawned at the point a hit landed. age accumulates
// each frame; once age >= lifetime the event is culled. killed marks a hit that
// dropped the target to zero health (the UI can color/scale these differently).
struct DamageEvent {
    glm::vec3 worldPos{0.f};
    int amount     = 0;
    bool killed    = false;
    float age      = 0.f;
    float lifetime = 1.0f;
};

// Transient crosshair flash. timer counts down to zero; kill distinguishes a
// finishing blow from an ordinary hit so the UI can flash a different color.
struct HitMarker {
    float timer = 0.f;
    bool kill   = false;
};

// Append a fresh damage number (age 0, default lifetime) for a landed hit.
inline void addDamageEvent(std::vector<DamageEvent>& events, glm::vec3 worldPos, int amount, bool killed) {
    DamageEvent ev;
    ev.worldPos = worldPos;
    ev.amount   = amount;
    ev.killed   = killed;
    events.push_back(ev);
}

// Age every damage number by dt and remove any whose age has reached lifetime.
inline void tickDamageEvents(std::vector<DamageEvent>& events, float dt) {
    for (DamageEvent& ev : events)
        ev.age += dt;
    std::erase_if(events, [](const DamageEvent& ev) { return ev.age >= ev.lifetime; });
}

// Result of projecting a world-space point into screen pixels. screenPos is in
// top-left-origin pixel coordinates (matches the UI convention); visible is
// false when the point is behind the camera or outside the view frustum.
struct ScreenProjection {
    glm::vec2 screenPos{0.f};
    bool visible = false;
};

// Project a world-space point through viewProj into screen pixels. Performs the
// perspective divide and maps NDC to pixels with a top-left origin
// (x grows right, y grows down). visible requires the point to be in front of
// the camera (clip w > 0) and inside the clip box: xy in [-1, 1] and, with
// GLM_FORCE_DEPTH_ZERO_TO_ONE, z in [0, 1]. screenW/screenH are the framebuffer
// dimensions in pixels.
inline ScreenProjection worldToScreen(const glm::mat4& viewProj, const glm::vec3& worldPos, int screenW, int screenH) {
    ScreenProjection out;
    glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.f);
    if (clip.w <= 0.f)
        return out;

    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    out.visible   = ndc.x >= -1.f && ndc.x <= 1.f && ndc.y >= -1.f && ndc.y <= 1.f && ndc.z >= 0.f && ndc.z <= 1.f;

    out.screenPos.x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(screenW);
    out.screenPos.y = (1.f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(screenH);
    return out;
}

// Light up the crosshair hit marker. duration defaults to a short flash; kill
// promotes the marker to its finishing-blow variant for the rest of the flash.
inline void triggerHitMarker(HitMarker& marker, bool kill) {
    marker.timer = 0.15f;
    marker.kill  = kill;
}

// Count the hit marker down by dt, clamping at zero. When it reaches zero the
// kill flag is cleared so the next ordinary hit starts from a clean state.
inline void tickHitMarker(HitMarker& marker, float dt) {
    marker.timer -= dt;
    if (marker.timer <= 0.f) {
        marker.timer = 0.f;
        marker.kill  = false;
    }
}

} // namespace ds
