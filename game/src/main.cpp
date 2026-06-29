#include "engine/Engine.h"
#include "engine/Paths.h"

#include <cstdlib>
#include <iostream>

int main(int /*argc*/, char* argv[]) {
    ds::paths::init(argv[0]);

    try {
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
