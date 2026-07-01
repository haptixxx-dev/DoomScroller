#include "engine/Paths.h"

#include "engine/BuildConfig.h"

#include <stdexcept>

namespace ds::paths {

namespace {
std::filesystem::path g_binDir;

// Root that holds the shipped assets/ and shaders/ trees. Normally the binary
// dir (flat layout: exe + assets/ + shaders/ side by side, as CPack stages on
// Windows/Linux). Inside a macOS .app bundle the exe lives at
// <App>.app/Contents/MacOS/DoomScroller, and CPack stages the data under
// <App>.app/Contents/Resources (Apple convention), so the resource root is the
// binary dir's sibling "Resources". Detect that and redirect; every other
// platform is unaffected.
std::filesystem::path resourceRoot() {
    if (g_binDir.filename() == "MacOS" && g_binDir.parent_path().filename() == "Contents") {
        std::filesystem::path resources = g_binDir.parent_path() / "Resources";
        if (std::filesystem::exists(resources)) {
            return resources;
        }
    }
    return g_binDir;
}
} // namespace

void init(const char* argv0) {
    g_binDir = std::filesystem::canonical(std::filesystem::path(argv0).parent_path());
}

std::filesystem::path binDir() {
    return g_binDir;
}

std::filesystem::path assets() {
#ifdef DS_DEV
    return std::filesystem::path(DS_SOURCE_DIR) / "assets";
#else
    return resourceRoot() / "assets";
#endif
}

std::filesystem::path shaders() {
#ifdef DS_DEV
    // Compiled bytecode lives in the binary dir, not source dir
    return std::filesystem::path(DS_BINARY_DIR) / "shaders";
#else
    return resourceRoot() / "shaders";
#endif
}

std::filesystem::path shaderSources() {
#ifdef DS_DEV
    return std::filesystem::path(DS_SOURCE_DIR) / "shaders";
#else
    return resourceRoot() / "shaders";
#endif
}

std::filesystem::path userDir() {
#ifdef DS_DEV
    return std::filesystem::path(DS_BINARY_DIR) / "user";
#else
    return g_binDir / "user";
#endif
}

} // namespace ds::paths
