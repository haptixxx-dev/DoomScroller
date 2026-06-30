#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace ds {

// CPU reference for the GPU tonemap pass (task 21). The `.slang` shader mirrors
// these exact formulas, so keep the two in lock-step. Everything operates on a
// linear HDR `glm::vec3` color and produces an LDR color in [0,1] (per channel).
// Pure, header-only, GLM-only — depends on nothing but <glm/glm.hpp> so it can
// be unit-tested against engine_headers.

// ---------------------------------------------------------------------------
// Exposure: a flat linear scale applied before any curve. `exposure` is a
// multiplier (1.0 == no change). Pure.
// ---------------------------------------------------------------------------
inline glm::vec3 applyExposure(const glm::vec3& hdr, float exposure) {
    return hdr * exposure;
}

// ---------------------------------------------------------------------------
// Reinhard: the classic c / (c + 1) curve, applied per channel. Maps [0, inf)
// into [0, 1); 0 -> 0 and 1 -> 0.5. Monotonic. Pure.
// ---------------------------------------------------------------------------
inline glm::vec3 reinhard(const glm::vec3& c) {
    return c / (c + glm::vec3(1.f));
}

// ---------------------------------------------------------------------------
// Extended Reinhard: c * (1 + c/white^2) / (1 + c), per channel. `whitePoint`
// is the luminance that maps to exactly 1.0, so values up to it are preserved
// and the rolloff is gentler than plain Reinhard. Pure.
// ---------------------------------------------------------------------------
inline glm::vec3 reinhardExtended(const glm::vec3& c, float whitePoint) {
    glm::vec3 numerator = c * (glm::vec3(1.f) + c / glm::vec3(whitePoint * whitePoint));
    return numerator / (glm::vec3(1.f) + c);
}

// ---------------------------------------------------------------------------
// ACES filmic (Krzysztof Narkowicz's analytic fit):
//   (c*(a*c + b)) / (c*(c*d + e) + f), clamped to [0,1] per channel,
// with a=2.51, b=0.03, d=2.43, e=0.59, f=0.14. Punchy contrast and a filmic
// shoulder; the de-facto default for HDR -> LDR. Pure.
// ---------------------------------------------------------------------------
inline glm::vec3 acesFilmic(const glm::vec3& c) {
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float d = 2.43f;
    constexpr float e = 0.59f;
    constexpr float f = 0.14f;
    glm::vec3 mapped  = (c * (a * c + glm::vec3(b))) / (c * (c * d + glm::vec3(e)) + glm::vec3(f));
    return glm::clamp(mapped, glm::vec3(0.f), glm::vec3(1.f));
}

// ---------------------------------------------------------------------------
// Selectable tonemap operator.
// ---------------------------------------------------------------------------
enum class TonemapOp : uint8_t {
    Reinhard,
    ReinhardExtended,
    ACES,
};

// Apply exposure, then the selected operator, then clamp the result to [0,1]
// (Reinhard variants never exceed 1 for non-negative input, but clamping also
// floors any negative HDR input at 0). `whitePoint` is only used by the
// extended-Reinhard operator. Pure.
inline glm::vec3 tonemap(const glm::vec3& hdr, float exposure, TonemapOp op, float whitePoint = 4.f) {
    glm::vec3 exposed = applyExposure(hdr, exposure);
    glm::vec3 result;
    switch (op) {
    case TonemapOp::ReinhardExtended:
        result = reinhardExtended(exposed, whitePoint);
        break;
    case TonemapOp::ACES:
        result = acesFilmic(exposed);
        break;
    case TonemapOp::Reinhard:
    default:
        result = reinhard(exposed);
        break;
    }
    return glm::clamp(result, glm::vec3(0.f), glm::vec3(1.f));
}

} // namespace ds
