// BC7 cook round-trip test (Phase 4 task 56).
//
// The BC7 *upload/sampling* path is GPU-gated (bench), but the ENCODER is pure
// CPU: this test cooks a real RGBA8 image to a BC7 block stream via the same
// bc7Cook + bc7enc path asset_cook uses, decodes it back with the vendored
// bc7decomp, and asserts the reconstruction is faithful (high PSNR) and the
// layout/size math is exact. That truth-tests the compressor end-to-end without
// a GPU. Also covers the pure Bc7Cook.h padding/block-iteration math directly.

#include "engine/Bc7Cook.h"

#include <bc7decomp.h>
#include <bc7enc.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace ds;

namespace {

// A deterministic, non-flat RGBA8 image so PSNR is meaningful (a flat image
// compresses losslessly and would pass trivially).
std::vector<uint8_t> makeGradient(uint32_t w, uint32_t h) {
    std::vector<uint8_t> img(static_cast<std::size_t>(w) * h * 4u);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4u;
            img[i + 0]    = static_cast<uint8_t>((x * 255u) / (w > 1u ? w - 1u : 1u));
            img[i + 1]    = static_cast<uint8_t>((y * 255u) / (h > 1u ? h - 1u : 1u));
            img[i + 2]    = static_cast<uint8_t>(((x + y) * 255u) / ((w + h) > 2u ? (w + h - 2u) : 1u));
            img[i + 3]    = 255u;
        }
    }
    return img;
}

// bc7enc-backed per-block compressor, matching asset_cook's setup.
struct Bc7Compressor {
    bc7enc_compress_block_params params{};
    Bc7Compressor() {
        bc7enc_compress_block_init();
        bc7enc_compress_block_params_init(&params);
        bc7enc_compress_block_params_init_linear_weights(&params);
    }
    void operator()(const uint8_t tile[64], uint8_t block[16]) { bc7enc_compress_block(block, tile, &params); }
};

// Decode a BC7 block stream back to a full RGBA8 image of (w,h), cropping the
// 4x4-block padding back to the original dimensions.
std::vector<uint8_t> decodeBc7(const std::vector<uint8_t>& blocks, uint32_t w, uint32_t h) {
    std::vector<uint8_t> out(static_cast<std::size_t>(w) * h * 4u, 0u);
    const uint32_t blocksX = bc7BlocksAcross(w);
    const uint32_t blocksY = bc7BlocksAcross(h);
    std::size_t offset     = 0;
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            bc7decomp::color_rgba texels[16];
            const bool ok = bc7decomp::unpack_bc7(blocks.data() + offset, texels);
            REQUIRE(ok);
            offset += 16u;
            for (uint32_t ty = 0; ty < 4u; ++ty) {
                const uint32_t py = by * 4u + ty;
                if (py >= h) {
                    continue;
                }
                for (uint32_t tx = 0; tx < 4u; ++tx) {
                    const uint32_t px = bx * 4u + tx;
                    if (px >= w) {
                        continue;
                    }
                    const bc7decomp::color_rgba& c = texels[ty * 4u + tx];
                    std::size_t o                  = (static_cast<std::size_t>(py) * w + px) * 4u;
                    out[o + 0]                     = c.m_comps[0];
                    out[o + 1]                     = c.m_comps[1];
                    out[o + 2]                     = c.m_comps[2];
                    out[o + 3]                     = c.m_comps[3];
                }
            }
        }
    }
    return out;
}

double psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    REQUIRE(a.size() == b.size());
    double mse = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        mse += d * d;
    }
    mse /= static_cast<double>(a.size());
    if (mse <= 0.0) {
        return 1000.0; // lossless
    }
    return 10.0 * std::log10((255.0 * 255.0) / mse);
}

} // namespace

TEST_CASE("bc7 payload size + block-count math", "[bc7]") {
    REQUIRE(bc7BlocksAcross(1) == 1);
    REQUIRE(bc7BlocksAcross(4) == 1);
    REQUIRE(bc7BlocksAcross(5) == 2);
    REQUIRE(bc7BlocksAcross(8) == 2);
    // 8x8 -> 2x2 blocks * 16 bytes = 64.
    REQUIRE(bc7PayloadSize(8, 8) == 64u);
    // 5x3 rounds up to 2x1 blocks = 32 bytes.
    REQUIRE(bc7PayloadSize(5, 3) == 32u);
}

TEST_CASE("bc7ExtractTile clamps edge texels into padding", "[bc7]") {
    // 5x5 image: the tile at block (1,1) covers x=4..7, y=4..7, all of which are
    // out of range except x=4,y=4 -> every texel must clamp to pixel (4,4).
    std::vector<uint8_t> img(5u * 5u * 4u, 0u);
    const std::size_t corner = (4u * 5u + 4u) * 4u;
    img[corner + 0]          = 11u;
    img[corner + 1]          = 22u;
    img[corner + 2]          = 33u;
    img[corner + 3]          = 44u;

    uint8_t tile[64];
    bc7ExtractTile(img.data(), 5, 5, 1, 1, tile);
    for (int t = 0; t < 16; ++t) {
        REQUIRE(tile[t * 4 + 0] == 11u);
        REQUIRE(tile[t * 4 + 1] == 22u);
        REQUIRE(tile[t * 4 + 2] == 33u);
        REQUIRE(tile[t * 4 + 3] == 44u);
    }
}

TEST_CASE("bc7Cook produces the exact payload size", "[bc7]") {
    auto img = makeGradient(16, 16);
    Bc7Compressor comp;
    auto blocks = bc7Cook(img.data(), 16, 16, std::ref(comp));
    REQUIRE(blocks.size() == bc7PayloadSize(16, 16)); // 4x4 blocks * 16 = 256
}

TEST_CASE("bc7 encode->decode round-trip is faithful (high PSNR)", "[bc7]") {
    auto img = makeGradient(64, 64);
    Bc7Compressor comp;
    auto blocks  = bc7Cook(img.data(), 64, 64, std::ref(comp));
    auto decoded = decodeBc7(blocks, 64, 64);
    // BC7 is a high-quality format; a smooth gradient should reconstruct well
    // above 40 dB. 40 dB is a conservative floor that still catches a broken
    // encoder (which would produce garbage well below it).
    const double db = psnr(img, decoded);
    INFO("PSNR = " << db << " dB");
    REQUIRE(db > 40.0);
}

TEST_CASE("bc7 handles non-multiple-of-4 dimensions", "[bc7]") {
    // 13x7: exercises edge padding on both axes; must not read OOB and must
    // reconstruct the in-bounds region reasonably. This case's POINT is padding
    // + size correctness, not peak quality: the gradient over so few texels is
    // very steep per 4x4 block (~42 levels/step on y), which is near BC7's
    // representable limit, so the PSNR floor here is lower than the smooth
    // 64x64 case above. >30 dB still catches a broken encoder (garbage is <20).
    auto img = makeGradient(13, 7);
    Bc7Compressor comp;
    auto blocks = bc7Cook(img.data(), 13, 7, std::ref(comp));
    REQUIRE(blocks.size() == bc7PayloadSize(13, 7)); // 4x2 blocks * 16 = 128
    auto decoded = decodeBc7(blocks, 13, 7);
    REQUIRE(psnr(img, decoded) > 30.0);
}

TEST_CASE("bc7Cook of an empty image is empty", "[bc7]") {
    Bc7Compressor comp;
    REQUIRE(bc7Cook(nullptr, 0, 0, std::ref(comp)).empty());
}
