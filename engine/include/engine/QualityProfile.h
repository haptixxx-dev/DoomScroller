#pragma once

#include "engine/rhi/RHITypes.h"

#include <cstdint>

namespace ds {

// Pure quality-profile selection (task 34). Maps a device's reported
// capabilities (`ds::rhi::RHICaps`) onto one of two render presets, then onto a
// concrete `QualityProfile` whose flags gate render-graph passes (shadows,
// bloom resolution, compute particles, etc.).
//
// This header is intentionally SDL3-free: it depends only on `RHITypes.h`
// (itself SDL3-free) + the C++ stdlib, so the decision logic is unit-testable
// against `engine_headers` with no GPU/window. Actually POPULATING `RHICaps`
// (deviceVRAMBytes, meshShaders, bindless, ...) from SDL3/native queries lives
// engine-side in the RHI device and is out of scope here.

// ---------------------------------------------------------------------------
// Tier: the coarse "how much GPU do we have" bucket.
//   Minimum  — baseline path (PLAN target: GTX 1060 3GB and similar).
//   Enhanced — full path (PLAN target: RTX 20xx / RDNA2 and up).
// ---------------------------------------------------------------------------
enum class QualityTier : uint8_t {
    Minimum,
    Enhanced,
};

// ---------------------------------------------------------------------------
// Concrete render preset. Each field directly toggles or sizes a render-graph
// resource/pass; Engine reads these at startup to decide what to build.
// ---------------------------------------------------------------------------
struct QualityProfile {
    QualityTier tier      = QualityTier::Minimum;
    bool shadows          = false; // build + run the shadow-map pass
    bool bloom            = true;  // bloom always on (cheap, defines the look)
    bool halfResBloom     = true;  // downsample bloom chain (true == cheaper)
    bool computeParticles = false; // GPU compute particle sim vs. CPU fallback
    int shadowMapSize     = 1024;  // square shadow-map resolution (px)
    float renderScale     = 1.f;   // backbuffer scale factor (1.0 == native)
};

// ---------------------------------------------------------------------------
// Tier selection threshold.
//
// Enhanced when the device looks like a modern discrete GPU:
//   * deviceVRAMBytes >= 6 GiB, OR
//   * mesh shaders supported, OR
//   * bindless supported.
// Otherwise Minimum. A reported VRAM of 0 means "unknown" and is treated
// conservatively as Minimum (we never assume capability we can't confirm).
//
// Rationale (see PLAN): a GTX 1060 3GB reports ~3 GiB and lacks mesh
// shaders/bindless -> Minimum. An RTX 20xx / RDNA2 part clears 6 GiB and/or
// advertises mesh shaders + bindless -> Enhanced.
// ---------------------------------------------------------------------------
constexpr uint64_t kEnhancedVRAMThreshold = 6ull * 1024ull * 1024ull * 1024ull; // 6 GiB

inline QualityTier selectTier(const ds::rhi::RHICaps& caps) {
    if (caps.meshShaders || caps.bindless) {
        return QualityTier::Enhanced;
    }
    if (caps.deviceVRAMBytes >= kEnhancedVRAMThreshold) {
        return QualityTier::Enhanced;
    }
    return QualityTier::Minimum;
}

// ---------------------------------------------------------------------------
// Concrete preset per tier. Pure lookup — no caps involved.
// ---------------------------------------------------------------------------
inline QualityProfile profileForTier(QualityTier tier) {
    QualityProfile p;
    p.tier = tier;
    switch (tier) {
    case QualityTier::Enhanced:
        p.shadows          = true;
        p.bloom            = true;
        p.halfResBloom     = false; // full-res bloom chain
        p.computeParticles = true;
        p.shadowMapSize    = 2048;
        p.renderScale      = 1.f;
        break;
    case QualityTier::Minimum:
    default:
        p.shadows          = false;
        p.bloom            = true;
        p.halfResBloom     = true; // cheaper downsampled bloom
        p.computeParticles = false;
        p.shadowMapSize    = 1024;
        p.renderScale      = 1.f;
        break;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Convenience: caps -> tier -> profile in one step.
// ---------------------------------------------------------------------------
inline QualityProfile profileForCaps(const ds::rhi::RHICaps& caps) {
    return profileForTier(selectTier(caps));
}

} // namespace ds
