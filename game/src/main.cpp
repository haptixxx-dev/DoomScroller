#include "engine/CaptureConfig.h"
#include "engine/Engine.h"
#include "engine/Paths.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

namespace {

// Offscreen golden-render capture entry (Phase 4 task 48). Builds the engine,
// renders one deterministic frame per CaptureScene into an LDR PPM (tagged by
// backend), and exits WITHOUT entering the interactive run loop. Driven by
//   DoomScroller --capture [outputDir]
// so a GPU bench can scriptably produce the task-47 golden references. NEEDS a
// GPU — cannot run in CI/sandbox; the bench operator eyeballs + commits the
// PPMs. `outputDir` defaults to the current directory when omitted.
int runCapture(std::string_view outputDir) {
    ds::EngineConfig cfg;
    cfg.title  = "DoomScroller (capture)";
    cfg.width  = 1280;
    cfg.height = 720;

    ds::Engine engine(cfg);

    int failures = 0;
    for (ds::CaptureScene scene : ds::kCaptureScenes) {
        const std::string path = engine.captureScene(scene, outputDir);
        if (path.empty()) {
            std::cerr << "[capture] FAILED scene '" << ds::captureSceneName(scene) << "'\n";
            ++failures;
        } else {
            std::cout << "[capture] wrote " << path << " (RENDER UNVERIFIED — eyeball before committing)\n";
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[]) {
    ds::paths::init(argv[0]);

    // Parse a minimal capture CLI before touching the interactive engine so a
    // headless-of-input bench run is fully scriptable. Everything else falls
    // through to the normal game loop (behaviour unchanged without --capture).
    std::string_view captureDir;
    bool capture = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--capture") {
            capture = true;
            // Optional next token is the output directory (not another flag).
            if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0)
                captureDir = argv[++i];
        }
    }

    try {
        if (capture)
            return runCapture(captureDir);

        ds::EngineConfig cfg;
        cfg.title  = "DoomScroller";
        cfg.width  = 1280;
        cfg.height = 720;

        ds::Engine engine(cfg);
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
