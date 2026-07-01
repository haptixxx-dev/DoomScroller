#pragma once

#include <cstdint>
#include <functional>
#include <vector>

// =============================================================================
// BC7 cook layout helper (Phase 4 task 56)
// =============================================================================
//
// The block-iteration + edge-padding math for cooking an 8-bit RGBA image into
// a BC7 block stream, kept SEPARATE from any actual BC7 encoder so it is pure
// and unit-testable (engine_headers only, no bc7enc dependency): the caller
// injects a `compressBlock` callback that turns one padded 4x4 RGBA tile (64
// bytes, row-major RGBA) into one 16-byte BC7 block. asset_cook supplies the
// real bc7enc-backed callback; tests can supply a stub or the real one.
//
// BC7 encodes 4x4 texel blocks at a fixed 16 bytes/block. Images whose width or
// height is not a multiple of 4 are padded to the next multiple of 4 by
// CLAMPING (replicating) the edge texels into the padding, which avoids a hard
// color discontinuity at the block edge that black/transparent padding would
// bake in. The decoded image is cropped back to the original dimensions by the
// consumer, so padding never becomes visible.
namespace ds {

// Blocks-per-row / -column for an image of the given dimension (rounded up).
inline uint32_t bc7BlocksAcross(uint32_t dim) {
    return (dim + 3u) / 4u;
}

// Total BC7 payload size in bytes for a width x height image (16 bytes/block).
inline uint32_t bc7PayloadSize(uint32_t width, uint32_t height) {
    return bc7BlocksAcross(width) * bc7BlocksAcross(height) * 16u;
}

// Extract the padded 4x4 RGBA tile whose top-left block coordinate is
// (blockX, blockY) from a tightly packed width*height*4 RGBA8 image. Texels
// outside [0,width) x [0,height) are clamped to the nearest edge texel. Writes
// 64 bytes (16 texels * RGBA) into `outTile` in row-major order.
inline void bc7ExtractTile(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t blockX, uint32_t blockY,
                           uint8_t outTile[64]) {
    for (uint32_t ty = 0; ty < 4u; ++ty) {
        // Clamp the source row into the image; edge rows replicate outward.
        uint32_t sy = blockY * 4u + ty;
        if (sy >= height) {
            sy = height - 1u;
        }
        for (uint32_t tx = 0; tx < 4u; ++tx) {
            uint32_t sx = blockX * 4u + tx;
            if (sx >= width) {
                sx = width - 1u;
            }
            const uint8_t* src = rgba + (static_cast<std::size_t>(sy) * width + sx) * 4u;
            uint8_t* dst       = outTile + (static_cast<std::size_t>(ty) * 4u + tx) * 4u;
            dst[0]             = src[0];
            dst[1]             = src[1];
            dst[2]             = src[2];
            dst[3]             = src[3];
        }
    }
}

// Cook a tightly packed width*height*4 RGBA8 image into a BC7 block stream.
// `compressBlock(tile64, out16)` must compress one padded 4x4 RGBA tile into
// one 16-byte BC7 block. Returns the concatenated block stream (row-major block
// order, matching bc7PayloadSize). An empty image yields an empty stream.
inline std::vector<uint8_t>
bc7Cook(const uint8_t* rgba, uint32_t width, uint32_t height,
        const std::function<void(const uint8_t tile[64], uint8_t block[16])>& compressBlock) {
    std::vector<uint8_t> out;
    if (width == 0u || height == 0u || rgba == nullptr) {
        return out;
    }
    const uint32_t blocksX = bc7BlocksAcross(width);
    const uint32_t blocksY = bc7BlocksAcross(height);
    out.resize(static_cast<std::size_t>(blocksX) * blocksY * 16u);

    uint8_t tile[64];
    std::size_t offset = 0;
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            bc7ExtractTile(rgba, width, height, bx, by, tile);
            compressBlock(tile, out.data() + offset);
            offset += 16u;
        }
    }
    return out;
}

} // namespace ds
