#pragma once

#include <cstdint>

namespace ds {

// Procedurally synthesized RGBA8 surface textures, generated on the CPU at
// startup in place of hand-authored image assets (no texture asset pipeline
// exists yet — TextureManager::load reads files via stb_image but nothing
// ships any). Deterministic (same params -> same pixels), so two textures
// built with the same seed are identical across runs/platforms.

// Deterministic 2D value noise in [0,1): bilinearly-interpolated, smoothstep-
// eased lattice noise from an integer hash (mirrors GameFeel.h's shakeNoise
// hash trick, extended to 2D and seeded so multiple textures don't share a
// pattern). No external noise library, no state.
float valueNoise2D(float x, float y, uint32_t seed);

// Parameters for generateProceduralTexture: a flat base color perturbed by
// layered value noise, optionally ruled into a tile/panel grid by darkened
// seam lines (gridSpacing == 0 disables the grid).
struct ProceduralTextureParams {
    uint8_t baseColor[3] = {180, 180, 180};
    float noiseScale     = 6.f;   // noise lattice cells spanning the texture
    float noiseStrength  = 0.18f; // +/- brightness perturbation (0 = flat)
    float gridSpacing    = 0.f;   // panel cells per axis; 0 = no grid lines
    float gridLineWidth  = 0.04f; // seam half-width, in cell-fractions
    float gridDarken     = 0.4f;  // brightness multiplier ON a seam pixel
    uint32_t seed        = 0;
};

// Fills outRGBA (width*height*4 bytes, row-major, opaque) with a tileable-
// looking structural surface: concrete/metal-panel style noise + optional
// grid seams. Used for level geometry and enemy hulls (see Engine.cpp).
void generateProceduralTexture(uint8_t* outRGBA, uint32_t width, uint32_t height,
                               const ProceduralTextureParams& params);

// Fills outRGBA with a radial "glow" blob: bright near-white core at the UV
// center fading to a dim rim, tinted by (r,g,b). Falloff is baked into RGB
// brightness (not alpha) since the mesh pipeline these textures feed renders
// opaque (no alpha blending) — see Engine.cpp's projectile material. Used for
// projectile/energy-bolt surfaces.
void generateGlowTexture(uint8_t* outRGBA, uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b);

} // namespace ds
