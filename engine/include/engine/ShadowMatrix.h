#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ds {

// Pure (engine-independent) CPU reference for the single-cascade sun shadow map
// (task 31): builds the light-space view-projection matrix used to render the
// scene's depth from the sun's point of view. Kept free of SDL3 / Jolt / EnTT so
// it can be unit-tested against engine_headers only.
//
// NOTE: the project builds with GLM_FORCE_DEPTH_ZERO_TO_ONE, so clip-space z is
// in [0,1] (D3D/Vulkan convention). glm::ortho() respects the define and emits a
// [0,1] depth-range projection, which is what the GPU shadow pass expects.

// World-space axis-aligned scene bounds. `min`/`max` are the per-component
// extremes (min.x <= max.x, etc.). Used to size the orthographic light frustum.
struct Bounds {
    glm::vec3 min{0.f};
    glm::vec3 max{0.f};

    // Geometric center of the box.
    glm::vec3 center() const { return (min + max) * 0.5f; }

    // Half the length of the box's space diagonal — the radius of the bounding
    // sphere that fully encloses the box. The orthographic light frustum is
    // sized from this so the whole scene is covered regardless of orientation.
    float radius() const { return glm::length(max - min) * 0.5f; }
};

// Build an orthographic light-space view-projection matrix that tightly covers
// `sceneBounds` from the direction the sun is shining (`sunDir` points the way
// the light travels, e.g. (0,-1,0) for a noon sun straight overhead).
//
// The light "eye" is placed at center - normalize(sunDir) * (radius + zPadding)
// — i.e. backed off along the incoming-light direction far enough to sit outside
// the scene — looking toward the center. The orthographic extents are
// [-radius, radius] in x and y, near = 0, far = 2*radius + 2*zPadding, so the
// scene's bounding sphere sits comfortably inside the depth range with
// `zPadding` slack on both the near and far sides.
//
// Up-vector degeneracy guard: glm::lookAt's up reference must not be parallel to
// the view direction or the cross products collapse to NaN. The default up is
// (0,1,0); when the sun is (nearly) vertical — |dot(dir,(0,1,0))| > 0.99 — we
// switch the up reference to (0,0,1) so the basis stays well-conditioned. This
// is exactly the straight-down sun case.
//
// Zero-direction guard: a ~zero `sunDir` cannot be normalized (glm::normalize
// would divide by zero and yield NaNs that poison the whole matrix). When the
// input length is below a small epsilon we fall back to a default downward sun
// direction (0,-1,0) so the matrix stays finite and well-formed.
inline glm::mat4 sunLightSpaceMatrix(const glm::vec3& sunDir, const Bounds& sceneBounds, float zPadding = 10.f) {
    constexpr float kMinDirLength = 1e-6f;
    const glm::vec3 dir    = glm::length(sunDir) < kMinDirLength ? glm::vec3{0.f, -1.f, 0.f} : glm::normalize(sunDir);
    const glm::vec3 center = sceneBounds.center();
    const float radius     = sceneBounds.radius();

    // Robust up reference: avoid the parallel-to-dir degeneracy.
    glm::vec3 up{0.f, 1.f, 0.f};
    if (glm::abs(glm::dot(dir, up)) > 0.99f)
        up = glm::vec3{0.f, 0.f, 1.f};

    const glm::vec3 eye = center - dir * (radius + zPadding);

    const glm::mat4 view = glm::lookAt(eye, center, up);
    const glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, 0.f, 2.f * radius + 2.f * zPadding);
    return proj * view;
}

// Transform a world-space position into the shadow map's clip space using the
// light-space matrix from sunLightSpaceMatrix(). Returns raw clip coordinates;
// the caller performs the perspective divide (xyz / w) before sampling. For an
// orthographic light matrix w == 1, so the divide is a no-op. Pure.
inline glm::vec4 worldToShadowUV(const glm::mat4& lightSpace, const glm::vec3& worldPos) {
    return lightSpace * glm::vec4(worldPos, 1.f);
}

} // namespace ds
