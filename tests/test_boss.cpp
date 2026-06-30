#include "engine/ecs/Components.h"

#include <catch2/catch_test_macros.hpp>

using namespace ds;

// bossPhaseForHealth (formerly BossLogic.h) and the attack-pattern logic moved
// to Lua (assets/scripts/boss.lua, module ds.boss) — see tests/test_boss_lua.cpp.
// BossComponent's struct shape stays C++, so its defaults are still tested here.
TEST_CASE("BossComponent defaults", "[boss]") {
    BossComponent b;
    REQUIRE(b.phase == 0);
    REQUIRE(b.maxHealth == 2000);
    REQUIRE(b.attackTimer == 0.f);
    REQUIRE(b.phaseHealthThresholds[0] == 0.66f);
}
