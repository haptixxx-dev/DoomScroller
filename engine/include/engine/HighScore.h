#pragma once

#include <filesystem>

namespace ds {

// Persistent high score, stored as a single integer in a small binary file
// under the binary directory. Reads are tolerant: a missing or unreadable file
// yields 0. Writes only overwrite when the new score is higher.
namespace highscore {

// Default storage path: <binDir>/highscore.dat.
std::filesystem::path defaultPath();

// Read the stored high score, or 0 if the file is missing/unreadable.
int load(const std::filesystem::path& path);

// Persist `score` if it beats the stored value. Returns true if a new record
// was written. Failures to write are swallowed (non-fatal).
bool save(const std::filesystem::path& path, int score);

} // namespace highscore

} // namespace ds
