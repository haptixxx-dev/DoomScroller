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

// Directory holding the .slang source files (for DS_DEV hot-reload).
// DS_DEV: <source_dir>/shaders/
// Shipping: same as shaders() (binary dir; no recompile happens there).
std::filesystem::path shaderSources();

// Absolute path to the running binary's directory.
std::filesystem::path binDir();

// Per-user writable directory for settings, saves, and other mutable state.
// DS_DEV: <binary_dir>/user/
// Shipping: <binDir>/user/  (a binary-relative writable dir)
//
// This resolver is deliberately SDL-free: Paths links no SDL, so it cannot call
// SDL_GetPrefPath (the platform-correct per-user location, e.g.
// %APPDATA% / ~/.local/share). A future refinement resolves the real pref path
// at the Engine layer (which has SDL) and passes it down; until then this
// binary-relative dir keeps settings/saves writable without a system dependency.
// userDir() only resolves the path; creating it is the caller's job (the
// save helpers in UserStorage.h create parent dirs on write).
std::filesystem::path userDir();

} // namespace ds::paths
