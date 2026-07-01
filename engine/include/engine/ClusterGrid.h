#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace ds {

// ---------------------------------------------------------------------------
// Clustered (froxel) lighting — pure CPU-side index math (task 59).
//
// Clustered / tiled lighting lifts the hard `kMaxForwardLights == 16` cap by
// partitioning the view frustum into a 3D grid of "froxels" (frustum voxels):
// tilesX * tilesY screen tiles, each sliced into sliceCount depth ranges. Each
// froxel stores the list of lights that touch it, so a fragment only shades the
// lights in its own cluster. The GPU assignment/culling pass and the storage
// buffers are bench/GPU work; the *index* math — view-space depth -> slice,
// screen pixel -> tile, and the (tileX, tileY, slice) -> flat cluster id
// flattener — is pure integer/float arithmetic and lands + tests here.
//
// Kept engine-independent (glm only, no RHI/GPU) so it is unit-testable against
// engine_headers, exactly like ShadowMatrix.h.
//
// Depth convention: view-space depth `z` is the POSITIVE linear distance from
// the camera along the view axis (i.e. -viewSpacePos.z for a right-handed
// camera looking down -Z). nearZ/farZ are the positive near/far plane
// distances. Slices are distributed exponentially (equal ratios in z), which
// matches how perspective projection compresses far geometry: this packs slice
// resolution near the camera where it matters.
// ---------------------------------------------------------------------------

// Configuration of the cluster grid. tilesX/tilesY are the screen-tile counts,
// sliceCount the number of exponential depth slices; nearZ/farZ bound the depth
// range the grid covers (positive distances, farZ > nearZ > 0).
struct ClusterGridConfig {
    uint32_t tilesX     = 16;
    uint32_t tilesY     = 9;
    uint32_t sliceCount = 24;
    float nearZ         = 0.1f;
    float farZ          = 100.f;
};

// Total number of clusters (froxels) in the grid: tilesX * tilesY * sliceCount.
// Each dimension is floored to at least 1 so the product is always positive and
// the valid cluster-index range is [0, clusterCount).
inline uint32_t clusterCount(const ClusterGridConfig& cfg) {
    const uint32_t tx = std::max(cfg.tilesX, 1u);
    const uint32_t ty = std::max(cfg.tilesY, 1u);
    const uint32_t sc = std::max(cfg.sliceCount, 1u);
    return tx * ty * sc;
}

// Map a view-space depth `z` (positive linear distance from the camera) to its
// exponential depth-slice index, clamped into [0, sliceCount - 1].
//
//   slice = floor( log(z / near) * sliceCount / log(far / near) )
//
// This is the standard logarithmic froxel slice distribution: z == nearZ maps
// to slice 0, z == farZ maps to the last slice (sliceCount - 1 after the
// clamp), and the index is monotonically non-decreasing in z. Inputs at or
// below nearZ clamp to 0; inputs at or beyond farZ clamp to the last slice, so
// out-of-range depths never produce an out-of-range (or overflowing) index.
inline uint32_t clusterSlice(float z, const ClusterGridConfig& cfg) {
    const uint32_t sliceCount = std::max(cfg.sliceCount, 1u);
    // Sanitise the range so the log/ratio is well-formed (zf > zn > 0). (`near`
    // / `far` are avoided as locals because <windows.h> #defines them.)
    const float zn = std::max(cfg.nearZ, 1e-4f);
    const float zf = std::max(cfg.farZ, zn + 1e-4f);

    // Everything at or nearer than the near plane belongs to slice 0.
    if (z <= zn)
        return 0u;
    // Everything at or beyond the far plane belongs to the last slice.
    if (z >= zf)
        return sliceCount - 1u;

    const float logRatio = std::log(zf / zn);
    const float raw      = std::log(z / zn) * static_cast<float>(sliceCount) / logRatio;
    // raw is in [0, sliceCount) for z in (near, far); floor + clamp guards float
    // drift at the edges.
    const int idx = static_cast<int>(std::floor(raw));
    return static_cast<uint32_t>(std::clamp(idx, 0, static_cast<int>(sliceCount) - 1));
}

// Map an integer screen-pixel coordinate to its tile coordinate on one axis.
// `pixel` is a 0-based pixel index, `viewportExtent` the viewport size in
// pixels on that axis, `tileCount` the number of tiles. Returns a tile index
// clamped into [0, tileCount - 1]; negative pixels clamp to tile 0 and pixels
// at/past the viewport edge clamp to the last tile, so the full screen is
// covered with no gaps or overlaps and out-of-range pixels never overflow.
inline uint32_t clusterTileAxis(int32_t pixel, uint32_t viewportExtent, uint32_t tileCount) {
    const uint32_t tiles  = std::max(tileCount, 1u);
    const uint32_t extent = std::max(viewportExtent, 1u);
    if (pixel <= 0)
        return 0u;
    if (static_cast<uint32_t>(pixel) >= extent)
        return tiles - 1u;
    // Scale the pixel into [0, tiles) by its fractional position across the
    // viewport. Using the pixel/extent ratio (rather than a fixed tile width)
    // keeps the tiling exact when extent is not a multiple of tiles.
    const float frac = static_cast<float>(pixel) / static_cast<float>(extent);
    const int idx    = static_cast<int>(frac * static_cast<float>(tiles));
    return static_cast<uint32_t>(std::clamp(idx, 0, static_cast<int>(tiles) - 1));
}

// Screen-pixel (x, y) -> tile (x, y). Convenience wrapper over clusterTileAxis
// for both axes, using the grid's tilesX/tilesY. `viewportW`/`viewportH` are the
// viewport size in pixels.
inline glm::uvec2 clusterTile(int32_t px, int32_t py, uint32_t viewportW, uint32_t viewportH,
                              const ClusterGridConfig& cfg) {
    return glm::uvec2{clusterTileAxis(px, viewportW, cfg.tilesX), clusterTileAxis(py, viewportH, cfg.tilesY)};
}

// Flatten a (tileX, tileY, slice) triple into a single cluster index. Layout is
// slice-major over the (tileY, tileX) plane:
//
//   index = slice * (tilesX * tilesY) + tileY * tilesX + tileX
//
// Every component is clamped into its valid range first, so the result is always
// in [0, clusterCount) — the mapping is a bijection over the valid
// (tileX, tileY, slice) domain and never overflows for out-of-range inputs.
inline uint32_t clusterIndex(uint32_t tileX, uint32_t tileY, uint32_t slice, const ClusterGridConfig& cfg) {
    const uint32_t tx = std::max(cfg.tilesX, 1u);
    const uint32_t ty = std::max(cfg.tilesY, 1u);
    const uint32_t sc = std::max(cfg.sliceCount, 1u);
    const uint32_t cx = std::min(tileX, tx - 1u);
    const uint32_t cy = std::min(tileY, ty - 1u);
    const uint32_t cs = std::min(slice, sc - 1u);
    return cs * (tx * ty) + cy * tx + cx;
}

// Inverse of clusterSlice: the [zNear, zFar) view-space depth bounds a given
// slice covers, following the same exponential distribution. Slice `s` spans
//   [ near * (far/near)^(s / sliceCount), near * (far/near)^((s+1) / sliceCount) )
// so bound 0 == nearZ and the far edge of the last slice == farZ. `slice` is
// clamped into [0, sliceCount - 1]. Returns {near-edge, far-edge}; a depth in
// [near-edge, far-edge) maps back to this slice via clusterSlice. Pure.
inline glm::vec2 clusterSliceBounds(uint32_t slice, const ClusterGridConfig& cfg) {
    const uint32_t sliceCount = std::max(cfg.sliceCount, 1u);
    const float zn            = std::max(cfg.nearZ, 1e-4f);
    const float zf            = std::max(cfg.farZ, zn + 1e-4f);
    const uint32_t s          = std::min(slice, sliceCount - 1u);

    const float ratio = zf / zn;
    const float lo    = zn * std::pow(ratio, static_cast<float>(s) / static_cast<float>(sliceCount));
    const float hi    = zn * std::pow(ratio, static_cast<float>(s + 1u) / static_cast<float>(sliceCount));
    return glm::vec2{lo, hi};
}

} // namespace ds
