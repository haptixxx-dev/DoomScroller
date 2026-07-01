#pragma once

#include "engine/rhi/RHITypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// =============================================================================
// Texture readback -> RGB8 conversion (Phase 4 task 48 pre-work)
// =============================================================================
//
// Pure, GPU-free helpers for turning a downloaded texture's raw bytes into an
// 8-bit RGB image (the P6 PPM payload the golden-image harness diffs). This is
// the CPU half of SDL3Device::debugDownloadTexture, factored out so it is
// unit-testable without a GPU and so the readback stops assuming "always 4
// bytes/px RGBA8 swizzled by the SWAPCHAIN format" — the original capture path
// could not read back the RGBA16Float HDR target at all.
//
// The caller passes the SOURCE texture's own format (the RHITexture handle is
// opaque and does not carry it), so an offscreen RGBA16Float / R32Float target
// converts correctly regardless of the swapchain format.
namespace ds::rhi {

// Bytes per texel for a format's linear, tightly packed layout. Used to size
// the download transfer buffer and stride the source rows. Depth formats return
// their storage size for completeness; the readback path is for color targets.
inline uint32_t bytesPerPixel(TextureFormat fmt) {
    switch (fmt) {
    case TextureFormat::R8Unorm:
        return 1u;
    case TextureFormat::RG8Unorm:
        return 2u;
    case TextureFormat::RGBA8Unorm:
    case TextureFormat::BGRA8Unorm:
    case TextureFormat::R32Float:
    case TextureFormat::D32Float:
    case TextureFormat::D24UnormS8Uint:
        return 4u;
    case TextureFormat::RGBA16Float:
        return 8u;
    case TextureFormat::BC7Unorm:
        return 0u; // block-compressed: not a per-pixel readback source
    }
    return 0u;
}

namespace detail {

// Decode one IEEE-754 half (binary16) to float. Pure bit manipulation, no
// hardware half support required.
inline float halfToFloat(uint16_t h) {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t exp  = (h >> 10) & 0x1Fu;
    const uint32_t mant = h & 0x3FFu;
    uint32_t bits;
    if (exp == 0u) {
        if (mant == 0u) {
            bits = sign; // +/- zero
        } else {
            // Subnormal: normalize.
            uint32_t e = 0u;
            uint32_t m = mant;
            while ((m & 0x400u) == 0u) {
                m <<= 1;
                ++e;
            }
            m &= 0x3FFu;
            const uint32_t fe = 127u - 15u - e + 1u;
            bits              = sign | (fe << 23) | (m << 13);
        }
    } else if (exp == 0x1Fu) {
        bits = sign | 0x7F800000u | (mant << 13); // inf / nan
    } else {
        const uint32_t fe = exp - 15u + 127u;
        bits              = sign | (fe << 23) | (mant << 13);
    }
    float out;
    static_assert(sizeof(out) == sizeof(bits));
    __builtin_memcpy(&out, &bits, sizeof(out));
    return out;
}

inline uint8_t clampToU8(float v) {
    if (v <= 0.f) {
        return 0u;
    }
    if (v >= 1.f) {
        return 255u;
    }
    return static_cast<uint8_t>(v * 255.f + 0.5f);
}

} // namespace detail

// Convert `pixelCount` texels of raw source bytes (tightly packed, format
// `fmt`) into a width*height*3 RGB8 buffer. Channel handling per format:
//   RGBA8Unorm  -> R,G,B passthrough (drop A)
//   BGRA8Unorm  -> swap B/R (drop A)
//   RG8Unorm    -> R,G,0
//   R8Unorm     -> R,R,R (grayscale)
//   R32Float    -> clamp(r) into all three channels (grayscale float)
//   RGBA16Float -> half-decode R,G,B, clamp to [0,1]*255 (NO tonemap — the HDR
//                  target is expected to already hold displayable values, or the
//                  caller tonemaps before capture; clamping keeps it lossless in
//                  range and deterministic for golden diffs)
// Unsupported formats (depth, BC7) yield an empty vector.
inline std::vector<uint8_t> convertToRgb8(const uint8_t* src, std::size_t pixelCount, TextureFormat fmt) {
    std::vector<uint8_t> out;
    const uint32_t bpp = bytesPerPixel(fmt);
    if (src == nullptr || pixelCount == 0u || bpp == 0u) {
        return out;
    }
    if (fmt == TextureFormat::D32Float || fmt == TextureFormat::D24UnormS8Uint) {
        return out; // depth is not a color readback target
    }
    out.resize(pixelCount * 3u);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const uint8_t* p = src + i * bpp;
        uint8_t r = 0, g = 0, b = 0;
        switch (fmt) {
        case TextureFormat::RGBA8Unorm:
            r = p[0];
            g = p[1];
            b = p[2];
            break;
        case TextureFormat::BGRA8Unorm:
            r = p[2];
            g = p[1];
            b = p[0];
            break;
        case TextureFormat::RG8Unorm:
            r = p[0];
            g = p[1];
            b = 0u;
            break;
        case TextureFormat::R8Unorm:
            r = g = b = p[0];
            break;
        case TextureFormat::R32Float: {
            float v;
            __builtin_memcpy(&v, p, sizeof(v));
            r = g = b = detail::clampToU8(v);
            break;
        }
        case TextureFormat::RGBA16Float: {
            uint16_t hr, hg, hb;
            __builtin_memcpy(&hr, p + 0, 2);
            __builtin_memcpy(&hg, p + 2, 2);
            __builtin_memcpy(&hb, p + 4, 2);
            r = detail::clampToU8(detail::halfToFloat(hr));
            g = detail::clampToU8(detail::halfToFloat(hg));
            b = detail::clampToU8(detail::halfToFloat(hb));
            break;
        }
        default:
            break;
        }
        out[i * 3 + 0] = r;
        out[i * 3 + 1] = g;
        out[i * 3 + 2] = b;
    }
    return out;
}

} // namespace ds::rhi
