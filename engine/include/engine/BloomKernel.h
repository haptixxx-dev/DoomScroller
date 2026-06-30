#pragma once

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace ds {

// Pure (engine-independent) CPU reference for the bloom post-process (task 26):
// the bright-pass prefilter and the separable Gaussian blur weights. The .slang
// bloom shader mirrors these exactly so the GPU result can be checked against a
// deterministic CPU computation. Kept free of SDL3 / Jolt / EnTT so it links
// against engine_headers only.

// ---------------------------------------------------------------------------
// Luminance: Rec. 709 luma of a linear-RGB color, dot(c, (0.2126,0.7152,0.0722)).
// White (1,1,1) -> 1, black -> 0. Used by the bright-pass to decide how much of
// a pixel survives the threshold. Pure.
// ---------------------------------------------------------------------------
inline float luminance(const glm::vec3& c) {
    return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

// ---------------------------------------------------------------------------
// Bright-pass prefilter with a soft (quadratic) knee, à la Unity's bloom.
//
// Let b = luminance(hdr). A hard threshold (keep b > threshold) produces a harsh
// edge where pixels pop in/out of the bloom. The soft knee blends across a band
// of width 2*knee centred on `threshold`:
//
//   soft        = clamp(b - threshold + knee, 0, 2*knee)
//   soft        = soft * soft / (4*knee + eps)         // smooth quadratic ramp
//   contribution= max(soft, b - threshold)             // hard part above the band
//   weight      = max(contribution, 0) / max(b, eps)   // scale factor in [0,1]
//
// The returned color is hdr * weight, so:
//   * b fully below the band (b <= threshold - knee) -> weight 0    -> vec3(0).
//   * b inside the band                              -> smooth ramp -> dim color.
//   * b well above the band                          -> contribution = b-threshold,
//     i.e. the original color scaled by its over-threshold factor (hue preserved).
// `knee` is the half-width of the blend band in luminance units. Pure.
// ---------------------------------------------------------------------------
inline glm::vec3 brightPass(const glm::vec3& hdr, float threshold, float knee = 0.5f) {
    constexpr float kEps = 1e-6f;
    float b              = luminance(hdr);

    float soft = b - threshold + knee;
    soft       = glm::clamp(soft, 0.f, 2.f * knee);
    soft       = soft * soft / (4.f * knee + kEps);

    float contribution = std::max(soft, b - threshold);
    float weight       = std::max(contribution, 0.f) / std::max(b, kEps);
    return hdr * weight;
}

// ---------------------------------------------------------------------------
// Separable Gaussian blur kernel. `weights` holds `taps` symmetric, normalized
// (sum == 1) coefficients with the center at index `radius`. Stored as a fixed
// array sized for the largest kernel the shader supports.
// ---------------------------------------------------------------------------
struct GaussianKernel {
    static constexpr int kMaxTaps = 9;
    float weights[kMaxTaps]{};
    int taps = 0;
};

// Build a 1-D Gaussian kernel for a separable blur. `radius` is clamped to
// [1, (kMaxTaps-1)/2] so `taps = 2*radius+1` never exceeds kMaxTaps. Weight at
// offset x (x in [-radius, radius]) is exp(-(x*x)/(2*sigma*sigma)), then the
// whole set is normalized so the weights sum to exactly 1. The center tap
// (index `radius`) is the largest; weights are symmetric about it. Pure.
inline GaussianKernel gaussianKernel(int radius, float sigma) {
    constexpr int kMaxRadius = (GaussianKernel::kMaxTaps - 1) / 2;
    radius                   = glm::clamp(radius, 1, kMaxRadius);

    GaussianKernel k;
    k.taps = 2 * radius + 1;

    // Guard against a non-positive sigma (division by zero -> NaN weights that
    // would poison the sum-normalization). Floor it to a small positive value.
    constexpr float kMinSigma = 1e-4f;
    sigma                     = std::max(sigma, kMinSigma);

    float sum             = 0.f;
    float twoSigmaSquared = 2.f * sigma * sigma;
    for (int i = 0; i < k.taps; ++i) {
        float x      = static_cast<float>(i - radius);
        float w      = std::exp(-(x * x) / twoSigmaSquared);
        k.weights[i] = w;
        sum += w;
    }
    for (int i = 0; i < k.taps; ++i)
        k.weights[i] /= sum;

    return k;
}

} // namespace ds
