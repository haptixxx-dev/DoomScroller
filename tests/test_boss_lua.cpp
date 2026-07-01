#include "engine/ScriptSystem.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <string>

using namespace ds;

namespace {

// Loads the real, shipped assets/scripts/boss.lua (DS_ASSETS_DIR is injected
// by tests/CMakeLists.txt), same pattern as test_parry_lua.cpp.
void loadBossScript(ScriptSystem& scripts) {
    REQUIRE(scripts.init());
    REQUIRE(scripts.loadFile(std::string(DS_ASSETS_DIR) + "/scripts/boss.lua"));
}

// bossTick with dt=0 and the boss far enough from the player that distance
// math doesn't matter for phase-only assertions.
BossTickResult tickPhaseOnly(ScriptSystem& scripts, int health, int maxHealth) {
    return scripts.bossTick(health, maxHealth, 0.f, glm::vec3{0.f}, glm::vec3{0.f, 0.f, 100.f}, 1u);
}

} // namespace

// Ports tests/test_boss.cpp's bossPhaseForHealth cases onto the Lua port,
// driven through ScriptSystem::bossTick (which also folds in the attack/
// vulnerable-window logic, so these assert phase only).
TEST_CASE("ds.boss phase advances as health crosses descending thresholds", "[scripting][boss]") {
    ScriptSystem scripts;
    loadBossScript(scripts);

    REQUIRE(tickPhaseOnly(scripts, 2000, 2000).phase == 0); // full health, phase 0
    scripts.bossReset();
    REQUIRE(tickPhaseOnly(scripts, 1400, 2000).phase == 0); // 70% still phase 0
    scripts.bossReset();
    REQUIRE(tickPhaseOnly(scripts, 1320, 2000).phase == 1); // exactly 66% -> phase 1
    scripts.bossReset();
    REQUIRE(tickPhaseOnly(scripts, 1000, 2000).phase == 1); // 50% phase 1
    scripts.bossReset();
    REQUIRE(tickPhaseOnly(scripts, 660, 2000).phase == 2); // 33% -> phase 2
    scripts.bossReset();
    REQUIRE(tickPhaseOnly(scripts, 200, 2000).phase == 2); // 10% phase 2
    scripts.bossReset();
    REQUIRE(tickPhaseOnly(scripts, 0, 2000).phase == 3); // dead -> final phase
}

TEST_CASE("ds.boss phase clamps negative health to the final phase", "[scripting][boss]") {
    ScriptSystem scripts;
    loadBossScript(scripts);
    REQUIRE(tickPhaseOnly(scripts, -50, 2000).phase == 3);
}

TEST_CASE("ds.boss phase handles zero/negative max defensively", "[scripting][boss]") {
    ScriptSystem scripts;
    loadBossScript(scripts);
    // boss.lua's threshold table is fixed at {0.66, 0.33, 0.0} (3 entries).
    REQUIRE(tickPhaseOnly(scripts, 100, 0).phase == 3);
}

TEST_CASE("ds.boss phase never advances backward as health drops", "[scripting][boss]") {
    ScriptSystem scripts;
    loadBossScript(scripts);
    int prev = 0;
    for (int hp = 2000; hp >= 0; hp -= 50) {
        int phase = tickPhaseOnly(scripts, hp, 2000).phase;
        REQUIRE(phase >= prev);
        prev = phase;
    }
}

TEST_CASE("ds.boss.reset clears phase/timers/pattern", "[scripting][boss]") {
    ScriptSystem scripts;
    loadBossScript(scripts);

    tickPhaseOnly(scripts, 0, 2000); // forces phase to the final one
    REQUIRE(scripts.doString("phase_before = ds.boss.phase"));
    REQUIRE(scripts.getGlobalNumber("phase_before") > 0.0);

    scripts.bossReset();
    REQUIRE(scripts.doString("phase_after = ds.boss.phase\n"
                              "attack_after = ds.boss.attack_timer\n"
                              "pattern_after = ds.boss.pattern"));
    REQUIRE(scripts.getGlobalNumber("phase_after") == 0.0);
    REQUIRE(scripts.getGlobalNumber("attack_after") == 2.0);
    REQUIRE(scripts.getGlobalNumber("pattern_after") == 0.0);
}

// New coverage: the attack loop fires the expected pellet count for a given
// phase (no prior C++ test exercised the pattern/cadence math, since it lived
// inline in Engine::bossSystem rather than in a testable function).
TEST_CASE("ds.boss.tick fires the expected pellet count and alternates pattern", "[scripting][boss]") {
    ScriptSystem scripts;
    ScriptSystem::Callbacks cb{};
    int fireCount = 0;
    cb.spawnProjectile = [&](const glm::vec3&, const glm::vec3&, int, uint32_t) { ++fireCount; };

    REQUIRE(scripts.init(cb));
    REQUIRE(scripts.loadFile(std::string(DS_ASSETS_DIR) + "/scripts/boss.lua"));

    glm::vec3 bossPos{0.f, 0.f, 0.f};
    glm::vec3 playerPos{0.f, 0.f, 10.f};

    // Phase 0: attack_timer starts at 2.0 (default ds.boss.attack_timer), so
    // tick past it to trigger the first volley. pattern=0 -> fan volley of 3.
    scripts.bossTick(2000, 2000, 2.1f, bossPos, playerPos, 1u);
    REQUIRE(fireCount == 3);

    // Second shot: pattern is now 1 -> charge burst, still phase 0 -> 3 pellets.
    fireCount               = 0;
    BossTickResult afterFirst = scripts.bossTick(2000, 2000, 0.f, bossPos, playerPos, 1u);
    // Re-tick past the new cadence to fire again.
    scripts.bossTick(2000, 2000, 3.f, bossPos, playerPos, 1u);
    REQUIRE(fireCount == 3);
    (void)afterFirst;
}

TEST_CASE("boss wrappers are graceful when boss.lua never loaded", "[scripting][boss]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());
    // No loadFile() call: ds.boss doesn't exist.
    scripts.bossReset();
    BossTickResult result = scripts.bossTick(1000, 2000, 0.016f, glm::vec3{0.f}, glm::vec3{0.f}, 1u);
    REQUIRE(result.phase == 0);
    REQUIRE(result.vulnerableTimer == 0.f);
    REQUIRE(result.pattern == 0);
}
