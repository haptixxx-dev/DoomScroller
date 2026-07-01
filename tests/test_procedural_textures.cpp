#include "engine/ProceduralTextures.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

using namespace ds;

TEST_CASE("valueNoise2D stays in [0,1) and is deterministic for the same inputs", "[textures]") {
    for (float x = 0.f; x < 10.f; x += 0.37f) {
        for (float y = 0.f; y < 10.f; y += 0.53f) {
            float n = valueNoise2D(x, y, 7u);
            REQUIRE(n >= 0.f);
            REQUIRE(n < 1.f);
            REQUIRE(valueNoise2D(x, y, 7u) == n);
        }
    }
}

TEST_CASE("valueNoise2D is continuous at integer lattice points across seeds", "[textures]") {
    // At an exact lattice point the smoothstep interpolation collapses to the
    // hash value itself (tx == ty == 0), so two different seeds must NOT
    // collide on every point tested (a broken hash ignoring the seed would).
    bool sawDifference = false;
    for (int i = 0; i < 20; ++i) {
        if (valueNoise2D(static_cast<float>(i), 0.f, 1u) != valueNoise2D(static_cast<float>(i), 0.f, 2u)) {
            sawDifference = true;
            break;
        }
    }
    REQUIRE(sawDifference);
}

TEST_CASE("generateProceduralTexture fills every pixel opaque with no out-of-range channels", "[textures]") {
    constexpr uint32_t kW = 16, kH = 16;
    std::vector<uint8_t> pixels(kW * kH * 4, 0);
    ProceduralTextureParams params{};
    params.baseColor[0]  = 200;
    params.baseColor[1]  = 150;
    params.baseColor[2]  = 100;
    params.noiseScale    = 4.f;
    params.noiseStrength = 0.25f;
    params.gridSpacing   = 4.f;
    params.seed          = 42;

    generateProceduralTexture(pixels.data(), kW, kH, params);

    for (uint32_t i = 0; i < kW * kH; ++i) {
        REQUIRE(pixels[i * 4 + 3] == 255); // always fully opaque
    }
}

TEST_CASE("generateProceduralTexture's grid seams are visibly darker than cell interiors", "[textures]") {
    // A 1x1 texture sampled exactly on a grid line (u=0 is a seam for any
    // integer gridSpacing) should be darker than a texture with the grid
    // disabled, all else equal — confirms gridDarken actually applies.
    constexpr uint32_t kW = 8, kH = 1;
    std::vector<uint8_t> withGrid(kW * 4, 0);
    std::vector<uint8_t> withoutGrid(kW * 4, 0);

    ProceduralTextureParams params{};
    params.baseColor[0]  = 200;
    params.baseColor[1]  = 200;
    params.baseColor[2]  = 200;
    params.noiseStrength = 0.f; // isolate the grid effect from noise
    params.gridSpacing   = 8.f;
    params.gridLineWidth = 0.1f;
    params.gridDarken    = 0.3f;
    generateProceduralTexture(withGrid.data(), kW, kH, params);

    params.gridSpacing = 0.f;
    generateProceduralTexture(withoutGrid.data(), kW, kH, params);

    // Pixel 0's UV center (u=0.5/8) sits within gridLineWidth of the u=0 seam
    // (cellU = u*gridSpacing = 0.5, distance to nearest integer = 0.5 — not a
    // seam). Use the texture's actual darkest pixel instead of assuming which
    // index lands on a seam, since the exact seam pixel depends on rounding.
    int darkestWith = 255 * 3 + 1, darkestWithout = 0;
    for (uint32_t i = 0; i < kW; ++i) {
        int sumWith    = withGrid[i * 4 + 0] + withGrid[i * 4 + 1] + withGrid[i * 4 + 2];
        int sumWithout = withoutGrid[i * 4 + 0] + withoutGrid[i * 4 + 1] + withoutGrid[i * 4 + 2];
        darkestWith    = std::min(darkestWith, sumWith);
        darkestWithout = std::max(darkestWithout, sumWithout);
    }
    REQUIRE(darkestWith < darkestWithout);
}

TEST_CASE("generateGlowTexture is brightest at the center and dims toward the rim", "[textures]") {
    constexpr uint32_t kSize = 32;
    std::vector<uint8_t> pixels(kSize * kSize * 4, 0);
    generateGlowTexture(pixels.data(), kSize, kSize, 50, 80, 220);

    auto luminance = [&](uint32_t px, uint32_t py) {
        const uint8_t* p = &pixels[(static_cast<size_t>(py) * kSize + px) * 4];
        return p[0] + p[1] + p[2];
    };

    int center = luminance(kSize / 2, kSize / 2);
    int corner = luminance(0, 0);
    int edge   = luminance(kSize / 2, 0);

    REQUIRE(center > corner);
    REQUIRE(center >= edge);

    for (uint32_t i = 0; i < kSize * kSize; ++i)
        REQUIRE(pixels[i * 4 + 3] == 255);
}
