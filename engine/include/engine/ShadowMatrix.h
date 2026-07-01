#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <vector>

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

// ---------------------------------------------------------------------------
// Point-light shadows (distance-based, 6 perspective faces — see
// shaders/point_shadow_depth.slang). Unlike the single-cascade sun map, a
// point light needs one frustum per cube face; these are the pure CPU-side
// matrix builders, kept engine-independent so they're unit-testable here
// (the GPU-side face rendering/sampling cannot be verified without hardware).
//
// Face index order: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z (the conventional
// OpenGL cube-map face order).
// ---------------------------------------------------------------------------

// World-space direction the given cube face looks toward.
inline glm::vec3 cubeFaceDirection(int face) {
    switch (face) {
    case 0:
        return glm::vec3{1.f, 0.f, 0.f};
    case 1:
        return glm::vec3{-1.f, 0.f, 0.f};
    case 2:
        return glm::vec3{0.f, 1.f, 0.f};
    case 3:
        return glm::vec3{0.f, -1.f, 0.f};
    case 4:
        return glm::vec3{0.f, 0.f, 1.f};
    case 5:
    default:
        return glm::vec3{0.f, 0.f, -1.f};
    }
}

// Up reference for the given cube face. The +-Y faces use (0,0,1)/(0,0,-1)
// since their view direction is parallel to the default (0,1,0) up (the same
// degeneracy sunLightSpaceMatrix guards against for a straight-down sun).
inline glm::vec3 cubeFaceUp(int face) {
    switch (face) {
    case 2:
        return glm::vec3{0.f, 0.f, 1.f};
    case 3:
        return glm::vec3{0.f, 0.f, -1.f};
    default:
        return glm::vec3{0.f, -1.f, 0.f};
    }
}

// Perspective (90-degree FOV, square aspect) view-projection looking from
// `lightPos` toward cube face `face` (0..5, see cubeFaceDirection). nearZ/farZ
// size the frustum; callers should set farZ to the light's own falloff radius
// so the shadow frustum and the light's attenuation agree (a fragment beyond
// the light's radius contributes zero light regardless of shadow result).
// NOTE: the project builds with GLM_FORCE_DEPTH_ZERO_TO_ONE (see
// sunLightSpaceMatrix's note above) — glm::perspective() respects the define
// and emits a [0,1] depth-range projection.
inline glm::mat4 pointShadowFaceMatrix(const glm::vec3& lightPos, int face, float nearZ, float farZ) {
    const glm::mat4 view = glm::lookAt(lightPos, lightPos + cubeFaceDirection(face), cubeFaceUp(face));
    const glm::mat4 proj = glm::perspective(glm::radians(90.f), 1.f, nearZ, farZ);
    return proj * view;
}

// ---------------------------------------------------------------------------
// Cascaded shadow maps + soft-shadow bias (task 59, pure math seams).
//
// A single ortho cascade sized to the whole scene wastes shadow resolution on
// distant geometry; splitting the view frustum into a few depth slices and
// fitting one ortho map per slice keeps near-camera shadows crisp. The split
// scheme, the per-cascade fit, the acne/peter-panning bias offset, and the PCF
// kernel tap tables are all pure — no RHI, no GPU — so they land + test here
// while the actual array-texture rendering stays bench-gated.
// ---------------------------------------------------------------------------

// Practical Split Scheme (Zhang et al.) cascade split distances. Returns the FAR
// distance of each of `numCascades` cascades along the view direction, in
// increasing order; the near edge of cascade i is the far edge of cascade i-1
// (with the first cascade's near edge == nearZ). The result therefore has
// `numCascades` entries, the last of which equals `farZ`.
//
// Each split blends a logarithmic distribution (equal ratios — ideal for
// perspective, packs resolution near the camera) with a uniform distribution
// (equal spacing) by `lambda` in [0,1]:
//   d_i = lambda * (near * (far/near)^(i/N))            // logarithmic
//       + (1 - lambda) * (near + (far - near) * (i/N))  // uniform
// lambda == 0 is pure uniform, lambda == 1 is pure logarithmic; the usual
// practical value is ~0.5. Inputs are clamped to be well-formed (numCascades >=
// 1, far > near > 0, lambda in [0,1]).
inline std::vector<float> cascadeSplitDistances(float nearZ, float farZ, int numCascades, float lambda) {
    const int count = std::max(numCascades, 1);
    // Guard the log distribution against a non-positive near plane (division /
    // ratio would blow up); nudge to a tiny positive epsilon.
    const float safeNear = std::max(nearZ, 1e-4f);
    const float safeFar  = std::max(farZ, safeNear + 1e-4f);
    const float lam      = std::clamp(lambda, 0.f, 1.f);
    const float ratio    = safeFar / safeNear;

    std::vector<float> splits;
    splits.reserve(static_cast<std::size_t>(count));
    for (int i = 1; i <= count; ++i) {
        const float p       = static_cast<float>(i) / static_cast<float>(count);
        const float logDist = safeNear * std::pow(ratio, p);
        const float uniDist = safeNear + (safeFar - safeNear) * p;
        splits.push_back(lam * logDist + (1.f - lam) * uniDist);
    }
    // Pin the final split exactly to farZ (float pow/blend can drift a hair).
    splits.back() = safeFar;
    return splits;
}

// Fit an orthographic light-space view-projection matrix to a sub-frustum of the
// camera view, given that sub-frustum's 8 world-space corners. This is the
// per-cascade analogue of sunLightSpaceMatrix: instead of the whole scene, it
// tightly wraps the corners of one cascade slice so the cascade spends its
// resolution only on the geometry that slice covers.
//
// The corners are transformed into the light's view space (a lookAt from a point
// backed off along -sunDir through the corners' centroid); the ortho box is the
// axis-aligned bounds of the corners in that space. `zPadding` pulls the near
// plane back so shadow casters *between* the light and the slice still render.
// Uses the same up-vector degeneracy guard as sunLightSpaceMatrix. Pure.
inline glm::mat4 sunCascadeMatrix(const glm::vec3& sunDir, const std::array<glm::vec3, 8>& frustumCorners,
                                  float zPadding = 10.f) {
    constexpr float kMinDirLength = 1e-6f;
    const glm::vec3 dir = glm::length(sunDir) < kMinDirLength ? glm::vec3{0.f, -1.f, 0.f} : glm::normalize(sunDir);

    glm::vec3 centroid{0.f};
    for (const glm::vec3& c : frustumCorners)
        centroid += c;
    centroid /= static_cast<float>(frustumCorners.size());

    glm::vec3 up{0.f, 1.f, 0.f};
    if (glm::abs(glm::dot(dir, up)) > 0.99f)
        up = glm::vec3{0.f, 0.f, 1.f};

    // Back the eye off along the incoming light so all corners sit in front of
    // the near plane; the exact distance is refined by the z-bounds below.
    const glm::vec3 eye  = centroid - dir;
    const glm::mat4 view = glm::lookAt(eye, centroid, up);

    // Axis-aligned bounds of the corners in light view space.
    glm::vec3 mn{std::numeric_limits<float>::max()};
    glm::vec3 mx{std::numeric_limits<float>::lowest()};
    for (const glm::vec3& c : frustumCorners) {
        const glm::vec3 v = glm::vec3(view * glm::vec4(c, 1.f));
        mn                = glm::min(mn, v);
        mx                = glm::max(mx, v);
    }

    // In right-handed view space the camera looks down -z, so the corners have
    // negative z; the ortho near/far are expressed as positive distances
    // (glm::ortho(left,right,bottom,top,near,far)). Pad the near side so casters
    // between the light and the slice are captured.
    const float nearZ = -mx.z - zPadding;
    const float farZ  = -mn.z + zPadding;

    const glm::mat4 proj = glm::ortho(mn.x, mx.x, mn.y, mx.y, nearZ, farZ);
    return proj * view;
}

// World-space bias offset that cures shadow acne (self-shadowing on lit
// surfaces) and peter-panning (shadows detaching from the caster). Pushes the
// sampled position along the surface normal by roughly one shadow texel, scaled
// up on grazing surfaces where the normal is near-perpendicular to the light and
// a single texel spans more depth.
//
// `texelWorldSize` is the world-space footprint of one shadow-map texel (light
// frustum width / shadow map resolution). `slopeScale` (>= 0) adds extra offset
// as the surface tilts away from the light; a value ~1-2 is typical. `normal`
// need not be normalized. Returns a vector to ADD to the world position before
// projecting into the shadow map. Pure.
inline glm::vec3 normalOffsetBias(const glm::vec3& normal, float texelWorldSize, float slopeScale) {
    const float len = glm::length(normal);
    if (len < 1e-6f)
        return glm::vec3{0.f};
    const glm::vec3 n     = normal / len;
    const float slope     = std::max(slopeScale, 0.f);
    const float magnitude = texelWorldSize * (1.f + slope);
    return n * magnitude;
}

// PCF (percentage-closer filtering) tap offsets for a 3x3 kernel, in shadow-map
// texel units, normalized so the offsets span [-1,1] on each axis (multiply by
// the per-texel UV size before sampling). The 9 taps are the center plus the 8
// neighbours; averaging their depth comparisons softens the shadow edge. Pure
// data — deterministic, symmetric about the origin, sums to (0,0).
inline std::array<glm::vec2, 9> pcfKernel3x3() {
    return std::array<glm::vec2, 9>{{
        {-1.f, -1.f},
        {0.f, -1.f},
        {1.f, -1.f},
        {-1.f, 0.f},
        {0.f, 0.f},
        {1.f, 0.f},
        {-1.f, 1.f},
        {0.f, 1.f},
        {1.f, 1.f},
    }};
}

// PCF tap offsets for a 5x5 kernel (25 taps), same convention as pcfKernel3x3:
// normalized to [-1,1], symmetric about the origin, sums to (0,0). A wider
// kernel gives softer penumbrae at more sampling cost.
inline std::array<glm::vec2, 25> pcfKernel5x5() {
    std::array<glm::vec2, 25> taps{};
    std::size_t idx = 0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            taps[idx++] = glm::vec2{static_cast<float>(x) * 0.5f, static_cast<float>(y) * 0.5f};
        }
    }
    return taps;
}

} // namespace ds
