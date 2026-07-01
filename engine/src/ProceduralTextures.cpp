#include "engine/ProceduralTextures.h"

#include <algorithm>
#include <cmath>

namespace ds {

namespace {

// Integer lattice hash -> [0,1). Splitmix-style avalanche so adjacent lattice
// points (and different seeds) don't visibly correlate.
float latticeHash(int ix, int iy, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(ix) * 374761393u + static_cast<uint32_t>(iy) * 668265263u + seed * 2246822519u +
                 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float smoothstep01(float t) {
    return t * t * (3.f - 2.f * t);
}

uint8_t toByte(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
}

} // namespace

float valueNoise2D(float x, float y, uint32_t seed) {
    float fx0 = std::floor(x);
    float fy0 = std::floor(y);
    int x0    = static_cast<int>(fx0);
    int y0    = static_cast<int>(fy0);
    float tx  = smoothstep01(x - fx0);
    float ty  = smoothstep01(y - fy0);

    float v00 = latticeHash(x0, y0, seed);
    float v10 = latticeHash(x0 + 1, y0, seed);
    float v01 = latticeHash(x0, y0 + 1, seed);
    float v11 = latticeHash(x0 + 1, y0 + 1, seed);

    float a = v00 + (v10 - v00) * tx;
    float b = v01 + (v11 - v01) * tx;
    return a + (b - a) * ty;
}

void generateProceduralTexture(uint8_t* outRGBA, uint32_t width, uint32_t height,
                               const ProceduralTextureParams& params) {
    for (uint32_t py = 0; py < height; ++py) {
        for (uint32_t px = 0; px < width; ++px) {
            float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(width);
            float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(height);

            // Two noise octaves (base lattice + a finer one at 2.7x the
            // frequency, half the weight) so the surface reads as irregular
            // grain rather than a single blurry blob.
            float n0 = valueNoise2D(u * params.noiseScale, v * params.noiseScale, params.seed);
            float n1 = valueNoise2D(u * params.noiseScale * 2.7f, v * params.noiseScale * 2.7f, params.seed + 1u);
            float n  = n0 * 0.7f + n1 * 0.3f;

            float brightness = 1.f + (n - 0.5f) * 2.f * params.noiseStrength;

            if (params.gridSpacing > 0.f) {
                float cellU = u * params.gridSpacing;
                float cellV = v * params.gridSpacing;
                float distU = std::abs(cellU - std::round(cellU));
                float distV = std::abs(cellV - std::round(cellV));
                bool onSeam = distU < params.gridLineWidth || distV < params.gridLineWidth;
                if (onSeam)
                    brightness *= params.gridDarken;
            }

            uint8_t* p = outRGBA + (static_cast<size_t>(py) * width + px) * 4;
            p[0]       = toByte(static_cast<float>(params.baseColor[0]) / 255.f * brightness);
            p[1]       = toByte(static_cast<float>(params.baseColor[1]) / 255.f * brightness);
            p[2]       = toByte(static_cast<float>(params.baseColor[2]) / 255.f * brightness);
            p[3]       = 255u;
        }
    }
}

void generateGlowTexture(uint8_t* outRGBA, uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b) {
    for (uint32_t py = 0; py < height; ++py) {
        for (uint32_t px = 0; px < width; ++px) {
            float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(width);
            float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(height);

            float dx   = u - 0.5f;
            float dy   = v - 0.5f;
            float dist = std::sqrt(dx * dx + dy * dy) / 0.5f; // 0 at center, ~1.41 at corners

            // Bright falloff toward the rim, never fully dark (a faint glow
            // halo past the inscribed circle reads better than a hard cutoff
            // against the box's flat UV mapping).
            float falloff = std::clamp(1.f - dist, 0.15f, 1.f);
            falloff       = smoothstep01(falloff);

            // Blend the tint toward white near the very center for a hot core.
            float coreMix = std::clamp(1.f - dist * 2.f, 0.f, 1.f);
            float cr      = static_cast<float>(r) / 255.f + (1.f - static_cast<float>(r) / 255.f) * coreMix;
            float cg      = static_cast<float>(g) / 255.f + (1.f - static_cast<float>(g) / 255.f) * coreMix;
            float cb      = static_cast<float>(b) / 255.f + (1.f - static_cast<float>(b) / 255.f) * coreMix;

            uint8_t* p = outRGBA + (static_cast<size_t>(py) * width + px) * 4;
            p[0]       = toByte(cr * falloff);
            p[1]       = toByte(cg * falloff);
            p[2]       = toByte(cb * falloff);
            p[3]       = 255u;
        }
    }
}

} // namespace ds
