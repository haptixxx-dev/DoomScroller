#include "engine/HighScore.h"

#include "engine/Paths.h"

#include <fstream>

namespace ds::highscore {

std::filesystem::path defaultPath() {
    return paths::binDir() / "highscore.dat";
}

int load(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return 0;
    int value = 0;
    f.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!f || value < 0)
        return 0;
    return value;
}

bool save(const std::filesystem::path& path, int score) {
    if (score <= load(path))
        return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    f.write(reinterpret_cast<const char*>(&score), sizeof(score));
    return static_cast<bool>(f);
}

} // namespace ds::highscore
