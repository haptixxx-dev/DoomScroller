#pragma once

#include <filesystem>

namespace ds::paths {

// Call once at startup with argv[0] to anchor the binary-relative paths.
void init(const char* argv0);

// Root of the asset tree.
// DS_DEV: <source_dir>/assets/
// Shipping: <binary_dir>/assets/
std::filesystem::path assets();

// Shader source/bytecode directory.
// DS_DEV: <source_dir>/shaders/
// Shipping: <binary_dir>/shaders/
std::filesystem::path shaders();

// Absolute path to the running binary's directory.
std::filesystem::path binDir();

} // namespace ds::paths
