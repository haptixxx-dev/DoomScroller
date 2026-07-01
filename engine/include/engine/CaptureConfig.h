#pragma once

#include <array>
#include <string>
#include <string_view>

// =============================================================================
// Offscreen golden-render capture harness — pure config (Phase 4 task 48)
// =============================================================================
//
// The GPU-side capture path (Engine::captureFrame) renders one deterministic
// frame of a fixed scene into an offscreen LDR target and dumps it to a P6 PPM
// via SDL3Device::debugDownloadTexture, so a GPU bench can produce the golden
// reference images the task-47 harness (engine/GoldenImage.h) diffs against.
//
// This header is the pure, GPU-free half: the enumeration of which deterministic
// scenes exist and the byte-stable output-filename scheme. It links nothing but
// the standard library, so it is unit-testable in the headless engine_math /
// engine_headers test target with no GPU. The Engine side consumes it to know
// what to render and where to write; the bench operator reads the same scheme to
// find the PPMs to eyeball and commit.
namespace ds {

// The fixed deterministic scenes the harness can capture. Each renders the
// current world at a pinned state (no RNG, no time-based animation advanced —
// see Engine::captureFrame), so a reference captured on the bench is stable and
// reproducible across runs on the same backend.
//
// NOTE (task 48, unverified without a GPU): today the engine builds a single
// scene at construction (the arena + Menu backdrop). `Startup` captures exactly
// that frame. The enum is kept open so the bench can add pinned variants (e.g.
// a wave spawned deterministically) without reworking the filename scheme.
enum class CaptureScene {
    Startup, // arena + Menu overlay, first frame, no update() ticked
    Arena,   // game started (wave spawned, lights live) + a deterministic
             // emissive burst + transient light, ticked a fixed number of
             // fixed-dt frames so HDR rolloff, bloom, dynamic point lights,
             // sun shadows and live particles are all actually exercised.
             // The bench's startup golden proved the pipeline but truth-tested
             // NONE of the 5 render features (empty menu backdrop); this scene
             // exists to fix that (see docs/bench-task48-verdict.md).
};

// All capture scenes, in a fixed order. Kept in sync with CaptureScene.
inline constexpr std::array<CaptureScene, 2> kCaptureScenes = {
    CaptureScene::Startup,
    CaptureScene::Arena,
};

// Stable, lower-case, filesystem-safe identifier for a scene. Used verbatim in
// the output filename, so changing one invalidates existing golden references.
inline constexpr std::string_view captureSceneName(CaptureScene scene) {
    switch (scene) {
    case CaptureScene::Startup:
        return "startup";
    case CaptureScene::Arena:
        return "arena";
    }
    return "unknown";
}

// Parse a scene name back to its enum (the inverse of captureSceneName). Returns
// false for an unrecognised name so a CLI arg can be validated before rendering.
inline bool captureSceneFromName(std::string_view name, CaptureScene& out) {
    for (CaptureScene scene : kCaptureScenes) {
        if (captureSceneName(scene) == name) {
            out = scene;
            return true;
        }
    }
    return false;
}

// Backend tag baked into the reference filename. The same scene renders to
// slightly different bytes per graphics backend (BGRA vs RGBA swapchain, F16
// rounding), so goldens are stored per backend and compareImages() diffs like
// against like. The bench passes the running backend's tag.
enum class CaptureBackend {
    Unknown,
    Vulkan,
    D3D12,
    Metal,
};

inline constexpr std::string_view captureBackendName(CaptureBackend backend) {
    switch (backend) {
    case CaptureBackend::Vulkan:
        return "vulkan";
    case CaptureBackend::D3D12:
        return "d3d12";
    case CaptureBackend::Metal:
        return "metal";
    case CaptureBackend::Unknown:
        break;
    }
    return "unknown";
}

// Map an SDL3 GPU driver name (SDL_GetGPUDeviceDriver, e.g. "vulkan", "direct3d12",
// "metal") to a CaptureBackend tag. Case-insensitive on the leading token so the
// exact SDL spelling per platform doesn't leak into the filename scheme.
inline CaptureBackend captureBackendFromDriver(std::string_view driver) {
    // Lower-case compare without allocating.
    auto startsWithCi = [](std::string_view s, std::string_view prefix) {
        if (s.size() < prefix.size())
            return false;
        for (std::size_t i = 0; i < prefix.size(); ++i) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
            if (c != prefix[i])
                return false;
        }
        return true;
    };
    if (startsWithCi(driver, "vulkan"))
        return CaptureBackend::Vulkan;
    if (startsWithCi(driver, "direct3d12") || startsWithCi(driver, "d3d12"))
        return CaptureBackend::D3D12;
    if (startsWithCi(driver, "metal"))
        return CaptureBackend::Metal;
    return CaptureBackend::Unknown;
}

// Build the reference-image path for a scene on a backend, rooted at `dir`
// (which may be empty for the current directory, and may or may not end in a
// separator — a single '/' is inserted only when needed). The layout is
//   <dir>/golden_<scene>_<backend>.ppm
// deliberately flat + deterministic so both the capture writer and the bench
// operator resolve the exact same path with no globbing. Uses '/' as the
// separator (valid on every platform SDL_IOFromFile targets).
inline std::string captureOutputPath(std::string_view dir, CaptureScene scene, CaptureBackend backend) {
    std::string out;
    out.reserve(dir.size() + 40);
    if (!dir.empty()) {
        out.append(dir);
        if (out.back() != '/' && out.back() != '\\')
            out.push_back('/');
    }
    out.append("golden_");
    out.append(captureSceneName(scene));
    out.push_back('_');
    out.append(captureBackendName(backend));
    out.append(".ppm");
    return out;
}

} // namespace ds
