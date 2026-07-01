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

// ---------------------------------------------------------------------------
// Gate predicate for the Enhanced-tier mesh-shader render path (task 57). The
// path is used only when BOTH the chosen profile is Enhanced AND the device
// actually advertises mesh shaders — a device can clear the Enhanced VRAM
// threshold (so profile.tier == Enhanced) without supporting mesh shaders, in
// which case the classic vertex path must be used. Pure predicate; the actual
// mesh pipeline (via nativeDevice()) + a cross-compilable mesh_ms shader are
// bench/GPU work (Slang compiles a mesh stage to SPIRV here, but the Metal
// backend rejects the naive form and DXIL needs dxc — so authoring a shader
// that ships on all three backends is deferred to the hardware pass).
// ---------------------------------------------------------------------------
inline bool useMeshShaders(const QualityProfile& profile, const ds::rhi::RHICaps& caps) {
    return profile.tier == QualityTier::Enhanced && caps.meshShaders;
}

// ---------------------------------------------------------------------------
// Sanitize raw device-query values into a trustworthy RHICaps before tier
// selection (task 55). The RHI device populates a raw RHICaps from whatever the
// backend can portably report and hands it here before storing/consuming it. A
// query can return implausible values (a driver reporting VRAM as UINT64_MAX,
// or feature bits set on a device whose maxTextureDim is absurdly small — a
// sign the query itself is unreliable). This pure normalizer defends selectTier
// from that garbage; the actual query stays engine-side (SDL3Device, out of
// scope here).
//
// Rules:
//   * VRAM above a sane ceiling (1 TiB) is treated as "unknown" -> 0, so a
//     bogus huge value cannot force Enhanced.
//   * If maxTextureDim is 0 (query clearly failed to populate), the feature
//     bits are not trusted and are cleared, and VRAM is zeroed -> conservative
//     Minimum. A real device always reports a nonzero max texture dimension.
//   * Otherwise the values pass through unchanged.
//
// NOTE: this deliberately does NOT gate Enhanced on a VRAM floor combined with
// the feature bits. Today deviceVRAMBytes is a stub 0 (see PLAN task 55: SDL3
// GPU exposes no VRAM/mesh/bindless query and no reachable native handle, so
// the device path reports conservative defaults), so a hard "feature AND
// VRAM>=floor" rule would starve the Enhanced path on real mesh-shader hardware
// until a VRAM query lands. selectTier keeps the feature-OR-VRAM rule; this
// sanitizer only removes clearly-invalid inputs.
// ---------------------------------------------------------------------------
constexpr uint64_t kMaxPlausibleVRAM = 1024ull * 1024ull * 1024ull * 1024ull; // 1 TiB

inline ds::rhi::RHICaps capsFromRawQuery(ds::rhi::RHICaps raw) {
    if (raw.deviceVRAMBytes > kMaxPlausibleVRAM) {
        raw.deviceVRAMBytes = 0; // implausible -> unknown
    }
    if (raw.maxTextureDim == 0u) {
        // Query did not populate; do not trust any capability it reported.
        raw.meshShaders     = false;
        raw.bindless        = false;
        raw.rayTracing      = false;
        raw.deviceVRAMBytes = 0;
    }
    return raw;
}

} // namespace ds
