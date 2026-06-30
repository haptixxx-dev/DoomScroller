// level_convert — offline converter that emits a binary .dslv level file.
//
// Usage:
//   level_convert <output.dslv> [input.txt]
//
// With an input text file, the file is parsed (engine/LevelTextParser.h) into a
// LevelData and written out as binary; on a parse error the message is printed
// and the tool exits non-zero. With no input file it falls back to emitting the
// hardcoded DoomScroller arena (the same room Engine::buildArena generates plus
// the enemy spawn corners and a placeholder light) for back-compat.
//
// The on-disk format (engine/LevelFormat.h) is identical either way.

#include "engine/LevelFormat.h"
#include "engine/LevelLoader.h"
#include "engine/LevelTextParser.h"

#include <cstdio>
#include <optional>
#include <string>

using namespace ds;

namespace {

BoxRecord makeBox(float cx, float cy, float cz, float hx, float hy, float hz) {
    BoxRecord b{};
    b.center[0]      = cx;
    b.center[1]      = cy;
    b.center[2]      = cz;
    b.halfExtents[0] = hx;
    b.halfExtents[1] = hy;
    b.halfExtents[2] = hz;
    b.color[0]       = 1.f;
    b.color[1]       = 1.f;
    b.color[2]       = 1.f;
    b.materialRef    = 0;
    return b;
}

SpawnPointRecord makeSpawn(float x, float y, float z, uint32_t flags) {
    SpawnPointRecord s{};
    s.position[0] = x;
    s.position[1] = y;
    s.position[2] = z;
    s.flags       = flags;
    return s;
}

// Build the in-memory arena: six slabs (floor/ceiling/4 walls), matching the
// collision boxes from Engine::buildArena, plus spawn points and one light.
LevelData buildArenaLevel() {
    constexpr float W = 10.f, H = 5.f, D = 10.f, T = 0.1f;

    LevelData data;
    data.boxes.push_back(makeBox(0.f, -T, 0.f, W, T, D));           // floor
    data.boxes.push_back(makeBox(0.f, H + T, 0.f, W, T, D));        // ceiling
    data.boxes.push_back(makeBox(0.f, H / 2, -D - T, W, H / 2, T)); // north wall
    data.boxes.push_back(makeBox(0.f, H / 2, D + T, W, H / 2, T));  // south wall
    data.boxes.push_back(makeBox(-W - T, H / 2, 0.f, T, H / 2, D)); // west wall
    data.boxes.push_back(makeBox(W + T, H / 2, 0.f, T, H / 2, D));  // east wall

    data.spawns.push_back(makeSpawn(0.f, 1.7f, 0.f, 1u)); // player start
    data.spawns.push_back(makeSpawn(-7.f, 1.5f, -7.f, 0u));
    data.spawns.push_back(makeSpawn(7.f, 1.5f, -7.f, 0u));
    data.spawns.push_back(makeSpawn(0.f, 1.5f, 7.f, 0u));

    LightRecord l{};
    l.position[0] = 0.f;
    l.position[1] = H - 0.5f;
    l.position[2] = 0.f;
    l.color[0]    = 1.f;
    l.color[1]    = 1.f;
    l.color[2]    = 1.f;
    l.radius      = 20.f;
    l.intensity   = 1.f;
    data.lights.push_back(l);

    return data;
}

// Read an entire file into a string. Returns false (and leaves `out` unspecified)
// if the file cannot be opened or read.
bool readFile(const char* path, std::string& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    out.clear();
    char buf[4096];
    size_t got = 0;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, got);
    }
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "usage: %s <output.dslv> [input.txt]\n", argv[0]);
        return 2;
    }

    const char* outputPath = argv[1];

    LevelData data;
    if (argc == 3) {
        const char* inputPath = argv[2];
        std::string text;
        if (!readFile(inputPath, text)) {
            std::fprintf(stderr, "level_convert: failed to read '%s'\n", inputPath);
            return 1;
        }
        std::string error;
        std::optional<LevelData> parsed = parseLevelText(text, &error);
        if (!parsed.has_value()) {
            std::fprintf(stderr, "level_convert: parse error in '%s': %s\n", inputPath, error.c_str());
            return 1;
        }
        data = std::move(*parsed);
    } else {
        data = buildArenaLevel();
    }

    if (!LevelLoader::write(outputPath, data)) {
        std::fprintf(stderr, "level_convert: failed to write '%s'\n", outputPath);
        return 1;
    }

    std::printf("level_convert: wrote %s (%zu boxes, %zu spawns, %zu lights)\n", outputPath, data.boxes.size(),
                data.spawns.size(), data.lights.size());
    return 0;
}
