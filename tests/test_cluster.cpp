#include "engine/ClusterGrid.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <set>

using Catch::Approx;
using namespace ds;

static ClusterGridConfig makeConfig() {
    // A small, easy-to-reason-about grid: 4x3 tiles, 8 depth slices, [1, 100].
    return ClusterGridConfig{4u, 3u, 8u, 1.f, 100.f};
}

TEST_CASE("clusterCount is the product of the three grid dimensions", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    REQUIRE(clusterCount(cfg) == 4u * 3u * 8u);
}

TEST_CASE("clusterCount floors each dimension to at least one", "[cluster]") {
    ClusterGridConfig cfg{0u, 0u, 0u, 1.f, 100.f};
    REQUIRE(clusterCount(cfg) == 1u); // 1 * 1 * 1, never zero
}

// -- slice index -----------------------------------------------------------

TEST_CASE("clusterSlice: z == near maps to slice 0", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    REQUIRE(clusterSlice(cfg.nearZ, cfg) == 0u);
}

TEST_CASE("clusterSlice: z == far maps to the last slice (clamped)", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    REQUIRE(clusterSlice(cfg.farZ, cfg) == cfg.sliceCount - 1u);
}

TEST_CASE("clusterSlice is monotonic non-decreasing in z", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    uint32_t prev         = 0u;
    // Sweep the whole depth range in fine steps; the slice index must never
    // decrease as z grows.
    for (float z = cfg.nearZ; z <= cfg.farZ; z += 0.25f) {
        const uint32_t s = clusterSlice(z, cfg);
        REQUIRE(s >= prev);
        REQUIRE(s < cfg.sliceCount); // always in range
        prev = s;
    }
}

TEST_CASE("clusterSlice covers every slice exactly once as z ramps", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    // With an exponential distribution, sweeping z from near to far should visit
    // all slices 0..sliceCount-1 (none skipped, none missing).
    std::set<uint32_t> seen;
    for (float z = cfg.nearZ; z <= cfg.farZ; z += 0.05f)
        seen.insert(clusterSlice(z, cfg));
    for (uint32_t s = 0; s < cfg.sliceCount; ++s)
        REQUIRE(seen.count(s) == 1u);
}

TEST_CASE("clusterSlice clamps out-of-range depths instead of overflowing", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    REQUIRE(clusterSlice(-50.f, cfg) == 0u);                            // behind the camera
    REQUIRE(clusterSlice(0.f, cfg) == 0u);                              // at the origin
    REQUIRE(clusterSlice(cfg.nearZ * 0.5f, cfg) == 0u);                 // nearer than near
    REQUIRE(clusterSlice(cfg.farZ * 10.f, cfg) == cfg.sliceCount - 1u); // well past far
}

// -- slice <-> bounds round trip -------------------------------------------

TEST_CASE("clusterSliceBounds: first bound is near, last far edge is far", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    const glm::vec2 first = clusterSliceBounds(0u, cfg);
    REQUIRE(first.x == Approx(cfg.nearZ).epsilon(1e-4f));
    const glm::vec2 last = clusterSliceBounds(cfg.sliceCount - 1u, cfg);
    REQUIRE(last.y == Approx(cfg.farZ).epsilon(1e-4f));
}

TEST_CASE("clusterSliceBounds tile bounds abut with no gaps", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    for (uint32_t s = 0; s + 1u < cfg.sliceCount; ++s) {
        const glm::vec2 a = clusterSliceBounds(s, cfg);
        const glm::vec2 b = clusterSliceBounds(s + 1u, cfg);
        REQUIRE(a.y == Approx(b.x).epsilon(1e-4f)); // slice s far edge == slice s+1 near edge
        REQUIRE(a.x < a.y);                         // each slice has positive extent
    }
}

TEST_CASE("clusterSlice(midpoint of a slice's bounds) returns that slice", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    for (uint32_t s = 0; s < cfg.sliceCount; ++s) {
        const glm::vec2 b    = clusterSliceBounds(s, cfg);
        const float midpoint = 0.5f * (b.x + b.y);
        REQUIRE(clusterSlice(midpoint, cfg) == s);
    }
}

TEST_CASE("clusterSliceBounds clamps an out-of-range slice to the last slice", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    const glm::vec2 last  = clusterSliceBounds(cfg.sliceCount - 1u, cfg);
    const glm::vec2 over  = clusterSliceBounds(cfg.sliceCount + 100u, cfg);
    REQUIRE(over.x == Approx(last.x).epsilon(1e-4f));
    REQUIRE(over.y == Approx(last.y).epsilon(1e-4f));
}

// -- tile mapping ----------------------------------------------------------

TEST_CASE("clusterTileAxis: pixel 0 -> tile 0, last pixel -> last tile", "[cluster]") {
    REQUIRE(clusterTileAxis(0, 800u, 4u) == 0u);
    REQUIRE(clusterTileAxis(799, 800u, 4u) == 3u);
}

TEST_CASE("clusterTileAxis covers the screen with no gaps or overlaps", "[cluster]") {
    // Every pixel in [0, extent) maps to exactly one tile; tiles are contiguous
    // and their union is the whole viewport. Verify by walking every pixel and
    // asserting the tile index is non-decreasing and spans 0..tiles-1.
    const uint32_t extent = 800u;
    const uint32_t tiles  = 4u;
    uint32_t prev         = 0u;
    std::set<uint32_t> seen;
    for (int32_t px = 0; px < static_cast<int32_t>(extent); ++px) {
        const uint32_t t = clusterTileAxis(px, extent, tiles);
        REQUIRE(t >= prev); // monotonic across the scanline
        REQUIRE(t < tiles); // in range
        seen.insert(t);
        prev = t;
    }
    for (uint32_t t = 0; t < tiles; ++t)
        REQUIRE(seen.count(t) == 1u); // no tile skipped -> full coverage
}

TEST_CASE("clusterTileAxis: tile boundaries fall on even pixel splits", "[cluster]") {
    // 800px / 4 tiles = 200px per tile: the first pixel of each tile is the split.
    REQUIRE(clusterTileAxis(199, 800u, 4u) == 0u);
    REQUIRE(clusterTileAxis(200, 800u, 4u) == 1u);
    REQUIRE(clusterTileAxis(399, 800u, 4u) == 1u);
    REQUIRE(clusterTileAxis(400, 800u, 4u) == 2u);
    REQUIRE(clusterTileAxis(600, 800u, 4u) == 3u);
}

TEST_CASE("clusterTileAxis clamps out-of-range pixels instead of overflowing", "[cluster]") {
    REQUIRE(clusterTileAxis(-100, 800u, 4u) == 0u); // negative -> tile 0
    REQUIRE(clusterTileAxis(800, 800u, 4u) == 3u);  // at the edge -> last tile
    REQUIRE(clusterTileAxis(5000, 800u, 4u) == 3u); // way past -> last tile
}

TEST_CASE("clusterTile maps both axes together", "[cluster]") {
    ClusterGridConfig cfg = makeConfig(); // 4x3 tiles
    const glm::uvec2 t    = clusterTile(0, 0, 800u, 600u, cfg);
    REQUIRE(t.x == 0u);
    REQUIRE(t.y == 0u);
    const glm::uvec2 br = clusterTile(799, 599, 800u, 600u, cfg);
    REQUIRE(br.x == cfg.tilesX - 1u);
    REQUIRE(br.y == cfg.tilesY - 1u);
}

// -- flattener -------------------------------------------------------------

TEST_CASE("clusterIndex(0,0,0) is 0", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    REQUIRE(clusterIndex(0u, 0u, 0u, cfg) == 0u);
}

TEST_CASE("clusterIndex is a bijection over the valid range", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    std::set<uint32_t> seen;
    for (uint32_t s = 0; s < cfg.sliceCount; ++s)
        for (uint32_t ty = 0; ty < cfg.tilesY; ++ty)
            for (uint32_t tx = 0; tx < cfg.tilesX; ++tx) {
                const uint32_t idx = clusterIndex(tx, ty, s, cfg);
                REQUIRE(idx < clusterCount(cfg)); // never out of range
                REQUIRE(seen.insert(idx).second); // every (tx,ty,s) is unique
            }
    // Injective + fills exactly clusterCount slots => bijection onto [0, count).
    REQUIRE(seen.size() == clusterCount(cfg));
}

TEST_CASE("clusterIndex layout is slice-major over (tileY, tileX)", "[cluster]") {
    ClusterGridConfig cfg = makeConfig(); // tilesX=4, tilesY=3, sliceCount=8
    // index = slice*(tilesX*tilesY) + tileY*tilesX + tileX
    REQUIRE(clusterIndex(1u, 0u, 0u, cfg) == 1u);                      // +1 in tileX
    REQUIRE(clusterIndex(0u, 1u, 0u, cfg) == cfg.tilesX);              // +1 in tileY == +tilesX
    REQUIRE(clusterIndex(0u, 0u, 1u, cfg) == cfg.tilesX * cfg.tilesY); // +1 slice
}

TEST_CASE("clusterIndex clamps out-of-range components", "[cluster]") {
    ClusterGridConfig cfg = makeConfig();
    // Out-of-range tile/slice values clamp to the last valid one rather than
    // overflowing past clusterCount.
    const uint32_t maxIdx = clusterIndex(cfg.tilesX - 1u, cfg.tilesY - 1u, cfg.sliceCount - 1u, cfg);
    REQUIRE(clusterIndex(999u, 999u, 999u, cfg) == maxIdx);
    REQUIRE(clusterIndex(999u, 999u, 999u, cfg) < clusterCount(cfg));
}
