#include "engine/Paths.h"

#include "engine/BuildConfig.h"

#include <stdexcept>

namespace ds::paths {

namespace {
std::filesystem::path g_binDir;
}

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
    return g_binDir / "assets";
#endif
}

std::filesystem::path shaders() {
#ifdef DS_DEV
    // Compiled bytecode lives in the binary dir, not source dir
    return std::filesystem::path(DS_BINARY_DIR) / "shaders";
#else
    return g_binDir / "shaders";
#endif
}

std::filesystem::path shaderSources() {
#ifdef DS_DEV
    return std::filesystem::path(DS_SOURCE_DIR) / "shaders";
#else
    return g_binDir / "shaders";
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
