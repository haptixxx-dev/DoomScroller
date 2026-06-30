#include "engine/ScriptSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

using namespace ds;

// Validates the Lua link + binding plumbing end-to-end: open a state, run a
// tiny inline script, and read back values both via raw globals and the typed
// config readers.
TEST_CASE("ScriptSystem runs inline Lua and reads back a value", "[scripting]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());
    REQUIRE(scripts.initialized());

    REQUIRE(scripts.doString("answer = 6 * 7"));
    REQUIRE(scripts.getGlobalNumber("answer", -1.0) == Catch::Approx(42.0));

    // Missing global falls back gracefully.
    REQUIRE(scripts.getGlobalNumber("nope", 99.0) == Catch::Approx(99.0));
}

// Wave config is fully Lua-side now (ds.wave.config, see test_wave_lua.cpp);
// only the enemy stat override table is still read back into a C++ struct
// (m_enemyStats, applied to spawned Grunts — the FSM logic itself moved to
// Lua in test_enemy_ai_lua.cpp, but per-archetype stat overrides did not).
TEST_CASE("ScriptSystem reads data-driven enemy stat overrides", "[scripting]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());

    REQUIRE(scripts.doString("ds.enemy_stats = { health = 250, speed = 6.5, damage = 30 }"));

    ScriptEnemyStats stats = scripts.enemyStats();
    REQUIRE(stats.overrode);
    REQUIRE(stats.health == 250);
    REQUIRE(stats.speed == Catch::Approx(6.5f));
    REQUIRE(stats.damage == 30);
}

TEST_CASE("ScriptSystem invokes C++ callbacks from Lua bindings", "[scripting]") {
    ScriptSystem scripts;
    ScriptSystem::Callbacks cb{};

    uint32_t spawnedType = 999;
    cb.spawnEnemy        = [&](float x, float y, float z, int type) -> uint32_t {
        spawnedType = static_cast<uint32_t>(type);
        return static_cast<uint32_t>(x + y + z);
    };
    std::string emitted;
    double emittedValue = 0.0;
    cb.emitEvent        = [&](std::string_view name, double value) {
        emitted      = std::string(name);
        emittedValue = value;
    };

    REQUIRE(scripts.init(cb));
    REQUIRE(scripts.doString("spawned = ds.spawn_enemy(1, 2, 3, 7)\n"
                             "ds.emit_event('boom', 12.5)"));

    REQUIRE(scripts.getGlobalNumber("spawned", -1.0) == Catch::Approx(6.0));
    REQUIRE(spawnedType == 7u);
    REQUIRE(emitted == "boom");
    REQUIRE(emittedValue == Catch::Approx(12.5));
}

TEST_CASE("ScriptSystem event callbacks are graceful when undefined", "[scripting]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());
    // No onWaveStart / onEnemyDeath / onPlayerDeath defined: must not throw/crash.
    scripts.onWaveStart(1);
    scripts.onEnemyDeath(0, 0.f, 0.f, 0.f);
    scripts.onPlayerDeath(123);
    SUCCEED();
}
