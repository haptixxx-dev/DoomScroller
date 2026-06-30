#include "engine/ScriptSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

using namespace ds;

// Exercises ds.Global (engine/script/LuaGlobal.*) the same way real gameplay
// scripts will: wire Callbacks::camera/player/time with lambdas writing into
// local test state (mirroring how Engine::initScripts() wires m_camera etc.),
// then read AND write through ds.Global from Lua and assert the C++-side state
// actually changed.
TEST_CASE("ds.Global.camera reads and writes live camera state", "[scripting][global]") {
    ScriptSystem scripts;
    ScriptSystem::Callbacks cb{};

    glm::vec3 camPos{1.f, 2.f, 3.f};
    float camYaw   = -90.f;
    float camPitch = 0.f;
    float camFovY  = 70.f;

    cb.camera.getPosition = [&] { return camPos; };
    cb.camera.setPosition = [&](const glm::vec3& p) { camPos = p; };
    cb.camera.getYaw       = [&] { return camYaw; };
    cb.camera.setYaw       = [&](float y) { camYaw = y; };
    cb.camera.getPitch     = [&] { return camPitch; };
    cb.camera.setPitch     = [&](float p) { camPitch = p; };
    cb.camera.getFovY      = [&] { return camFovY; };
    cb.camera.setFovY      = [&](float f) { camFovY = f; };

    REQUIRE(scripts.init(cb));

    REQUIRE(scripts.doString(R"lua(
        px, py, pz = ds.Global.camera.position.x, ds.Global.camera.position.y, ds.Global.camera.position.z
        yaw0, pitch0, fov0 = ds.Global.camera.yaw, ds.Global.camera.pitch, ds.Global.camera.fovY
        ds.Global.camera.position = Vec3.new(10, 20, 30)
        ds.Global.camera.yaw = 45
        ds.Global.camera.pitch = -10
        ds.Global.camera.fovY = 90
    )lua"));

    REQUIRE(scripts.getGlobalNumber("px") == Catch::Approx(1.0));
    REQUIRE(scripts.getGlobalNumber("py") == Catch::Approx(2.0));
    REQUIRE(scripts.getGlobalNumber("pz") == Catch::Approx(3.0));
    REQUIRE(scripts.getGlobalNumber("yaw0") == Catch::Approx(-90.0));
    REQUIRE(scripts.getGlobalNumber("pitch0") == Catch::Approx(0.0));
    REQUIRE(scripts.getGlobalNumber("fov0") == Catch::Approx(70.0));

    REQUIRE(camPos == glm::vec3{10.f, 20.f, 30.f});
    REQUIRE(camYaw == Catch::Approx(45.f));
    REQUIRE(camPitch == Catch::Approx(-10.f));
    REQUIRE(camFovY == Catch::Approx(90.f));
}

TEST_CASE("ds.Global.player exposes health read/write and read-only fields", "[scripting][global]") {
    ScriptSystem scripts;
    ScriptSystem::Callbacks cb{};

    int health = 80;
    cb.player.getHealth      = [&] { return health; };
    cb.player.setHealth      = [&](int v) { health = v; };
    cb.player.getMaxHealth   = [&] { return 100; };
    cb.player.getDashCharges = [&] { return 2; };
    cb.player.isSliding      = [&] { return true; };
    cb.player.getIFrames     = [&] { return 0.25f; };
    cb.player.getEyePosition = [&] { return glm::vec3{0.f, 1.7f, 0.f}; };

    REQUIRE(scripts.init(cb));

    REQUIRE(scripts.doString(R"lua(
        h0 = ds.Global.player.health
        maxh = ds.Global.player.maxHealth
        charges = ds.Global.player.dashCharges
        slide = ds.Global.player.sliding and 1 or 0
        iframes = ds.Global.player.iFrames
        eye_y = ds.Global.player.eyePosition.y
        ds.Global.player.health = 55
    )lua"));

    REQUIRE(scripts.getGlobalNumber("h0") == Catch::Approx(80.0));
    REQUIRE(scripts.getGlobalNumber("maxh") == Catch::Approx(100.0));
    REQUIRE(scripts.getGlobalNumber("charges") == Catch::Approx(2.0));
    REQUIRE(scripts.getGlobalNumber("slide") == 1.0);
    REQUIRE(scripts.getGlobalNumber("iframes") == Catch::Approx(0.25));
    REQUIRE(scripts.getGlobalNumber("eye_y") == Catch::Approx(1.7));
    REQUIRE(health == 55);

    // Read-only fields reject writes.
    REQUIRE_FALSE(scripts.doString("ds.Global.player.maxHealth = 999"));
    REQUIRE_FALSE(scripts.doString("ds.Global.player.sliding = false"));
}

TEST_CASE("ds.Global.time is read-only", "[scripting][global]") {
    ScriptSystem scripts;
    ScriptSystem::Callbacks cb{};
    cb.time.getDt      = [] { return 0.016f; };
    cb.time.getElapsed = [] { return 12.5f; };

    REQUIRE(scripts.init(cb));

    REQUIRE(scripts.doString("dt = ds.Global.time.dt elapsed = ds.Global.time.elapsed"));
    REQUIRE(scripts.getGlobalNumber("dt") == Catch::Approx(0.016));
    REQUIRE(scripts.getGlobalNumber("elapsed") == Catch::Approx(12.5));

    REQUIRE_FALSE(scripts.doString("ds.Global.time.dt = 1"));
}

TEST_CASE("ds.Global sub-tables are graceful with no callbacks wired", "[scripting][global]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());

    // No camera/player/time callbacks set: reads fall back to zero/false/nil
    // rather than crashing.
    REQUIRE(scripts.doString(R"lua(
        yaw = ds.Global.camera.yaw
        h = ds.Global.player.health
        dt = ds.Global.time.dt
    )lua"));
    REQUIRE(scripts.getGlobalNumber("yaw") == Catch::Approx(0.0));
    REQUIRE(scripts.getGlobalNumber("h") == Catch::Approx(0.0));
    REQUIRE(scripts.getGlobalNumber("dt") == Catch::Approx(0.0));
}
