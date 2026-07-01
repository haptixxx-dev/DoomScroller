#include "engine/ScriptSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace ds;

namespace {

struct CapturedBox {
    glm::vec3 center, halfExtents, color;
};
struct CapturedMesh {
    std::string path;
    glm::vec3 position;
    glm::quat rotation;
};
struct CapturedSpawn {
    glm::vec3 position;
    bool isPlayer;
    int archetypeHint;
};
struct CapturedLight {
    glm::vec3 position, color;
    float radius, intensity;
};

} // namespace

// Exercises ds.level.* (engine/ScriptSystem.h + engine/LevelGen.h's binding
// surface) directly through ScriptSystem, mirroring the established
// test_*_lua.cpp pattern: wire Callbacks to lambdas capturing local vectors,
// doString() inline Lua, assert on the captured values.
TEST_CASE("ds.level.add_box accumulates boxes", "[scripting][level]") {
    ScriptSystem scripts;
    ScriptSystem::Callbacks cb{};
    std::vector<CapturedBox> boxes;
    cb.levelAddBox = [&](glm::vec3 c, glm::vec3 h, glm::vec3 col) { boxes.push_back({c, h, col}); };

    REQUIRE(scripts.init(cb));
    REQUIRE(scripts.doString("ds.level.add_box(Vec3.new(1, 2, 3), Vec3.new(4, 5, 6), Vec3.new(1, 0, 0))\n"
                             "ds.level.add_box(Vec3.new(0, 0, 0), Vec3.new(1, 1, 1), Vec3.new(0, 1, 0))"));

    REQUIRE(boxes.size() == 2);
    REQUIRE(boxes[0].center == glm::vec3{1.f, 2.f, 3.f});
    REQUIRE(boxes[0].halfExtents == glm::vec3{4.f, 5.f, 6.f});
    REQUIRE(boxes[0].color == glm::vec3{1.f, 0.f, 0.f});
    REQUIRE(boxes[1].center == glm::vec3{0.f, 0.f, 0.f});
}

TEST_CASE("ds.level.add_mesh captures path/position and converts optional Euler degrees to a quaternion",
          "[scripting][level]") {
    ScriptSystem scripts;
    ScriptSystem::Callbacks cb{};
    std::vector<CapturedMesh> meshes;
    cb.levelAddMesh = [&](const std::string& path, glm::vec3 pos, glm::quat rot) {
        meshes.push_back({path, pos, rot});
    };

    REQUIRE(scripts.init(cb));
    REQUIRE(scripts.doString("ds.level.add_mesh('meshes/pillar.gltf', Vec3.new(3, 0, 3))\n"
                             "ds.level.add_mesh('meshes/ramp.gltf', Vec3.new(1, 1, 1), Vec3.new(0, 90, 0))"));

    REQUIRE(meshes.size() == 2);
    REQUIRE(meshes[0].path == "meshes/pillar.gltf");
    REQUIRE(meshes[0].position == glm::vec3{3.f, 0.f, 3.f});
    // No rotation arg -> identity quaternion.
    REQUIRE(meshes[0].rotation.x == Catch::Approx(0.f).margin(1e-5));
    REQUIRE(meshes[0].rotation.w == Catch::Approx(1.f).margin(1e-5));

    REQUIRE(meshes[1].path == "meshes/ramp.gltf");
    // 90 degrees about Y -> quat (0, sin(45deg), 0, cos(45deg)).
    REQUIRE(meshes[1].rotation.y == Catch::Approx(0.70710678f).margin(1e-4));
    REQUIRE(meshes[1].rotation.w == Catch::Approx(0.70710678f).margin(1e-4));
}

TEST_CASE("ds.level.add_spawn captures position, player flag, and archetype hint", "[scripting][level]") {
    ScriptSystem scripts;
    ScriptSystem::Callbacks cb{};
    std::vector<CapturedSpawn> spawns;
    cb.levelAddSpawn = [&](glm::vec3 pos, bool isPlayer, int archetypeHint) {
        spawns.push_back({pos, isPlayer, archetypeHint});
    };

    REQUIRE(scripts.init(cb));
    REQUIRE(scripts.doString("ds.level.add_spawn(Vec3.new(0, 1.7, 0), true)\n"
                             "ds.level.add_spawn(Vec3.new(5, 1.5, 5), false)\n"
                             "ds.level.add_spawn(Vec3.new(-5, 1.5, -5), false, 1)"));

    REQUIRE(spawns.size() == 3);
    REQUIRE(spawns[0].position == glm::vec3{0.f, 1.7f, 0.f});
    REQUIRE(spawns[0].isPlayer);
    REQUIRE(spawns[0].archetypeHint == -1);
    REQUIRE_FALSE(spawns[1].isPlayer);
    REQUIRE(spawns[1].archetypeHint == -1); // no archetype arg -> no hint
    REQUIRE_FALSE(spawns[2].isPlayer);
    REQUIRE(spawns[2].archetypeHint == 1);  // Charger
}

TEST_CASE("ds.level.add_light captures position/color/radius/intensity", "[scripting][level]") {
    ScriptSystem scripts;
    ScriptSystem::Callbacks cb{};
    std::vector<CapturedLight> lights;
    cb.levelAddLight = [&](glm::vec3 pos, glm::vec3 color, float radius, float intensity) {
        lights.push_back({pos, color, radius, intensity});
    };

    REQUIRE(scripts.init(cb));
    REQUIRE(scripts.doString("ds.level.add_light(Vec3.new(0, 4, 0), Vec3.new(1, 1, 1), 20, 1.5)"));

    REQUIRE(lights.size() == 1);
    REQUIRE(lights[0].position == glm::vec3{0.f, 4.f, 0.f});
    REQUIRE(lights[0].radius == Catch::Approx(20.f));
    REQUIRE(lights[0].intensity == Catch::Approx(1.5f));
}

TEST_CASE("ds.level.* calls are silent no-ops when the callbacks are unset", "[scripting][level]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init()); // empty Callbacks{}
    REQUIRE(scripts.doString("ds.level.add_box(Vec3.new(0, 0, 0), Vec3.new(1, 1, 1), Vec3.new(1, 1, 1))\n"
                             "ds.level.add_spawn(Vec3.new(0, 0, 0), true)\n"
                             "ds.level.add_light(Vec3.new(0, 0, 0), Vec3.new(1, 1, 1), 1, 1)\n"
                             "ds.level.add_mesh('x.gltf', Vec3.new(0, 0, 0))"));
}

// Loads the real, shipped assets/scripts/level.lua (DS_ASSETS_DIR is injected
// by tests/CMakeLists.txt) — a syntax/logic regression test on the shipped
// default script, matching the convention for other assets/scripts/*.lua tests.
TEST_CASE("the shipped assets/scripts/level.lua generates the expected counts", "[scripting][level]") {
    ScriptSystem scripts;
    ScriptSystem::Callbacks cb{};
    int boxCount = 0, spawnCount = 0, lightCount = 0;
    bool sawPlayerSpawn = false;
    cb.levelAddBox      = [&](glm::vec3, glm::vec3, glm::vec3) { ++boxCount; };
    cb.levelAddSpawn    = [&](glm::vec3, bool isPlayer, int) {
        ++spawnCount;
        if (isPlayer)
            sawPlayerSpawn = true;
    };
    cb.levelAddLight = [&](glm::vec3, glm::vec3, float, float) { ++lightCount; };

    REQUIRE(scripts.init(cb));
    REQUIRE(scripts.loadFile(std::string(DS_ASSETS_DIR) + "/scripts/level.lua"));

    // level.lua randomizes room count (2..4), per-room width/pillars/spawns,
    // and door-segment geometry each run (Lua 5.4 auto-seeds math.random
    // per-state), so this asserts ranges spanning every possible outcome
    // rather than exact counts:
    //   boxes:  4*rooms (floor/ceiling/N/S) + 1 (west wall) + 1 (east wall)
    //           + up to 2*(rooms-1) door segments + up to 2*rooms pillars
    //           -> [10, 32] across rooms in [2,4]
    //   spawns: 2..4 per room + 1 player -> [5, 17] across rooms in [2,4]
    //   lights: exactly 1 per room -> [2, 4]
    REQUIRE(boxCount >= 10);
    REQUIRE(boxCount <= 32);
    REQUIRE(spawnCount >= 5);
    REQUIRE(spawnCount <= 17);
    REQUIRE(lightCount >= 2);
    REQUIRE(lightCount <= 4);
    REQUIRE(sawPlayerSpawn);
}
