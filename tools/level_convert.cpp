// level_convert — offline converter that emits a binary .dslv level file.
//
// This is a minimal stub: it has no parser yet and simply emits the hardcoded
// DoomScroller arena (the same 20x5x20 room buildArena() generates plus the
// existing enemy spawn corners and a placeholder light) to the output path.
//
//   level_convert <output.dslv>
//
// A future revision can swap buildArena() for a text/JSON description parser;
// the on-disk format (engine/LevelFormat.h) stays the same.

#include "engine/LevelFormat.h"
#include "engine/LevelLoader.h"

#include <cstdio>
#include <cstring>

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

    data.spawns.push_back(makeSpawn(0.f, 1.7f, 0.f, 1u));           // player start
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

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output.dslv>\n", argv[0]);
        return 2;
    }

    LevelData data = buildArenaLevel();
    if (!LevelLoader::write(argv[1], data)) {
        std::fprintf(stderr, "level_convert: failed to write '%s'\n", argv[1]);
        return 1;
    }

    std::printf("level_convert: wrote %s (%zu boxes, %zu spawns, %zu lights)\n", argv[1], data.boxes.size(),
                data.spawns.size(), data.lights.size());
    return 0;
}
