#pragma once

#include <glm/glm.hpp>

namespace ds {

// Pure (engine-independent) CPU frustum-culling math. Used to reject objects
// whose bounds fall entirely outside the camera's view before submitting draws.
// Kept free of SDL3 / Jolt / EnTT so it can be unit-tested against
// engine_headers only.
//
// NOTE: the project builds with GLM_FORCE_DEPTH_ZERO_TO_ONE, so clip-space z is
// in [0,1] (D3D/Vulkan convention). extractFrustum() assumes that convention.

// Axis-aligned bounding box. `min`/`max` are the per-component extremes in world
// space (min.x <= max.x, etc.).
struct AABB {
    glm::vec3 min{0.f};
    glm::vec3 max{0.f};
};

// Build an AABB from a center point and (non-negative) half-extents along each
// axis. Pure.
inline AABB aabbFromCenterExtents(const glm::vec3& center, const glm::vec3& halfExtents) {
    return AABB{center - halfExtents, center + halfExtents};
}

// A plane in the form dot(normal, p) + d = 0. Signed-distance convention: a
// point p is inside the plane half-space (in front of the plane) when
// dot(normal, p) + d >= 0.
struct Plane {
    glm::vec3 normal{0.f};
    float d = 0.f;

    // Signed distance from the plane to point p; positive when p is inside the
    // half-space the normal points into. Assumes the plane is normalized.
    float signedDistance(const glm::vec3& p) const { return glm::dot(normal, p) + d; }

    // Scale normal and d so |normal| == 1, preserving the plane and its
    // half-space orientation. No-op for a degenerate (zero-length) normal.
    void normalize() {
        float len = glm::length(normal);
        if (len > 1e-8f) {
            float inv = 1.f / len;
            normal *= inv;
            d *= inv;
        }
    }
};

// Six bounding planes of a view frustum, each with its normal pointing inward.
// Order: left, right, bottom, top, near, far.
struct Frustum {
    Plane planes[6];
};

// Extract the six frustum planes from a combined view-projection matrix using
// the Gribb-Hartmann method, with inward-pointing normalized planes.
//
// glm matrices are column-major, so `m[col][row]`; "row i" of the matrix is
// (m[0][i], m[1][i], m[2][i], m[3][i]). Under GLM_FORCE_DEPTH_ZERO_TO_ONE the
// near plane is row3(col2) alone (z in [0,1]), not row3 + row2 as in OpenGL.
inline Frustum extractFrustum(const glm::mat4& m) {
    // Rows of the view-projection matrix.
    const glm::vec4 row0{m[0][0], m[1][0], m[2][0], m[3][0]};
    const glm::vec4 row1{m[0][1], m[1][1], m[2][1], m[3][1]};
    const glm::vec4 row2{m[0][2], m[1][2], m[2][2], m[3][2]};
    const glm::vec4 row3{m[0][3], m[1][3], m[2][3], m[3][3]};

    const glm::vec4 raw[6] = {
        row3 + row0, // left
        row3 - row0, // right
        row3 + row1, // bottom
        row3 - row1, // top
        row2,        // near (z in [0,1])
        row3 - row2, // far
    };

    Frustum f;
    for (int i = 0; i < 6; ++i) {
        f.planes[i].normal = glm::vec3(raw[i]);
        f.planes[i].d      = raw[i].w;
        f.planes[i].normalize();
    }
    return f;
}

// Conservative AABB-vs-frustum test using the positive-vertex (p-vertex)
// method. For each plane the box corner furthest along the plane normal is
// chosen; if even that corner is behind the plane the box is fully outside and
// we return false. May report false positives (boxes near a corner can survive)
// but never false negatives, which is the correct bias for culling. Pure.
inline bool aabbInFrustum(const Frustum& f, const AABB& box) {
    for (const Plane& plane : f.planes) {
        // p-vertex: the corner most in the direction of the plane normal.
        glm::vec3 pVertex{
            plane.normal.x >= 0.f ? box.max.x : box.min.x,
            plane.normal.y >= 0.f ? box.max.y : box.min.y,
            plane.normal.z >= 0.f ? box.max.z : box.min.z,
        };
        if (plane.signedDistance(pVertex) < 0.f)
            return false;
    }
    return true;
}

} // namespace ds
