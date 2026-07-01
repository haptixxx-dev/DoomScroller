// Unit tests for the pure golden-render capture config (Phase 4 task 48). This
// is the GPU-free half of the offscreen capture harness: scene enumeration,
// backend tagging, and the byte-stable output-filename scheme that both the
// capture writer (Engine::captureFrame) and the bench operator resolve. The
// render itself needs a GPU and is verified on the bench, not here.

#include "engine/CaptureConfig.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace ds;

TEST_CASE("capture scene names are stable and round-trip", "[capture]") {
    // Every enumerated scene has a non-empty, unique name that parses back to
    // itself. A drift here would silently rename existing golden references.
    for (CaptureScene scene : kCaptureScenes) {
        std::string_view name = captureSceneName(scene);
        REQUIRE_FALSE(name.empty());
        REQUIRE(name != "unknown");

        CaptureScene parsed{};
        REQUIRE(captureSceneFromName(name, parsed));
        REQUIRE(parsed == scene);
    }

    // The Startup scene's identifier is pinned (existing goldens depend on it).
    REQUIRE(captureSceneName(CaptureScene::Startup) == "startup");
}

TEST_CASE("unknown scene name does not parse", "[capture]") {
    CaptureScene out = CaptureScene::Startup;
    REQUIRE_FALSE(captureSceneFromName("does-not-exist", out));
    REQUIRE_FALSE(captureSceneFromName("", out));
    // out is left untouched on failure.
    REQUIRE(out == CaptureScene::Startup);
}

TEST_CASE("backend driver mapping is case-insensitive on the leading token", "[capture]") {
    REQUIRE(captureBackendFromDriver("vulkan") == CaptureBackend::Vulkan);
    REQUIRE(captureBackendFromDriver("Vulkan") == CaptureBackend::Vulkan);
    REQUIRE(captureBackendFromDriver("VULKAN") == CaptureBackend::Vulkan);
    REQUIRE(captureBackendFromDriver("direct3d12") == CaptureBackend::D3D12);
    REQUIRE(captureBackendFromDriver("D3D12") == CaptureBackend::D3D12);
    REQUIRE(captureBackendFromDriver("metal") == CaptureBackend::Metal);
    REQUIRE(captureBackendFromDriver("Metal") == CaptureBackend::Metal);
    REQUIRE(captureBackendFromDriver("") == CaptureBackend::Unknown);
    REQUIRE(captureBackendFromDriver("gnm") == CaptureBackend::Unknown);
}

TEST_CASE("backend names are stable", "[capture]") {
    REQUIRE(captureBackendName(CaptureBackend::Vulkan) == "vulkan");
    REQUIRE(captureBackendName(CaptureBackend::D3D12) == "d3d12");
    REQUIRE(captureBackendName(CaptureBackend::Metal) == "metal");
    REQUIRE(captureBackendName(CaptureBackend::Unknown) == "unknown");
}

TEST_CASE("output path is flat and deterministic", "[capture]") {
    REQUIRE(captureOutputPath("golden", CaptureScene::Startup, CaptureBackend::Vulkan) ==
            "golden/golden_startup_vulkan.ppm");
    REQUIRE(captureOutputPath("golden/", CaptureScene::Startup, CaptureBackend::Metal) ==
            "golden/golden_startup_metal.ppm");
    // A trailing backslash is also treated as a separator (Windows paths).
    REQUIRE(captureOutputPath("out\\", CaptureScene::Startup, CaptureBackend::D3D12) ==
            "out\\golden_startup_d3d12.ppm");
    // Empty dir => current directory, no leading separator.
    REQUIRE(captureOutputPath("", CaptureScene::Startup, CaptureBackend::Unknown) == "golden_startup_unknown.ppm");
}

TEST_CASE("output path filename is what captureOutputPath('') produces", "[capture]") {
    // The bench operator resolves the same path from the scheme; pin that the
    // scene+backend fully determine the trailing filename.
    const std::string full = captureOutputPath("some/dir", CaptureScene::Startup, CaptureBackend::Vulkan);
    const std::string bare = captureOutputPath("", CaptureScene::Startup, CaptureBackend::Vulkan);
    REQUIRE(full.size() > bare.size());
    REQUIRE(full.substr(full.size() - bare.size()) == bare);
}
