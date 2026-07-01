#include "engine/ScriptSystem.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace ds;

namespace {

constexpr int kGrunt   = 0;
constexpr int kCharger = 1;
constexpr int kRanged  = 2;

constexpr int kIdle   = 0;
constexpr int kChase  = 1;
constexpr int kAttack = 2;

void loadEnemyAiScript(ScriptSystem& scripts) {
    REQUIRE(scripts.init());
    REQUIRE(scripts.loadFile(std::string(DS_ASSETS_DIR) + "/scripts/enemy_ai.lua"));
}

// Grunt tuning matching EnemyArchetype.h's defaults closely enough for tests:
// moveSpeed=3, attackRange=1.5, detectionRange=15, attackInterval=1,
// chargeWindup=0, chargeSpeed=0.
EnemyAIDecision tickGrunt(ScriptSystem& scripts, int state, float dist, float cooldown) {
    return scripts.enemyAITick(kGrunt, state, dist, cooldown, 3.f, 1.5f, 15.f, 1.f, 0.f, 0.f);
}

EnemyAIDecision tickCharger(ScriptSystem& scripts, int state, float dist, float cooldown) {
    return scripts.enemyAITick(kCharger, state, dist, cooldown, 5.f, 2.5f, 18.f, 1.f, 0.6f, 16.f);
}

EnemyAIDecision tickRanged(ScriptSystem& scripts, int state, float dist, float cooldown) {
    return scripts.enemyAITick(kRanged, state, dist, cooldown, 2.f, 12.f, 22.f, 1.f, 0.f, 0.f);
}

} // namespace

TEST_CASE("ds.enemy_ai Idle transitions to Chase within detection range", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    EnemyAIDecision d = tickGrunt(scripts, kIdle, 5.f, 0.f); // detectionRange=15
    REQUIRE(d.state == kChase);
    REQUIRE(d.setVelocity);
    REQUIRE(d.moveIntent == 0.f);
}

TEST_CASE("ds.enemy_ai Idle stays Idle outside detection range", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    EnemyAIDecision d = tickGrunt(scripts, kIdle, 20.f, 0.f);
    REQUIRE(d.state == kIdle);
    REQUIRE(d.setVelocity);
    REQUIRE(d.moveIntent == 0.f);
}

TEST_CASE("ds.enemy_ai Chase moves forward at full speed mid-range", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    EnemyAIDecision d = tickGrunt(scripts, kChase, 8.f, 0.f); // between attackRange(1.5) and detectionRange(15)
    REQUIRE(d.state == kChase);
    REQUIRE(d.setVelocity);
    REQUIRE(d.moveIntent == 1.f);
}

TEST_CASE("ds.enemy_ai Chase leaves velocity untouched when losing the player", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    EnemyAIDecision d = tickGrunt(scripts, kChase, 20.f, 0.f); // beyond detectionRange(15)
    REQUIRE(d.state == kIdle);
    REQUIRE_FALSE(d.setVelocity);                              // original omits the setLinearVelocity call here
}

TEST_CASE("ds.enemy_ai Chase to Attack arms the charger windup, not other archetypes", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    EnemyAIDecision grunt = tickGrunt(scripts, kChase, 1.f, 0.f); // within attackRange(1.5)
    REQUIRE(grunt.state == kAttack);
    REQUIRE_FALSE(grunt.armWindup);

    EnemyAIDecision charger = tickCharger(scripts, kChase, 2.f, 0.f); // within attackRange(2.5)
    REQUIRE(charger.state == kAttack);
    REQUIRE(charger.armWindup);
}

TEST_CASE("ds.enemy_ai Grunt attack: melee fires on cooldown, always zeroes velocity", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    EnemyAIDecision ready = tickGrunt(scripts, kAttack, 1.f, 0.f); // in range, cooldown elapsed
    REQUIRE(ready.state == kAttack);
    REQUIRE(ready.meleeAttack);
    REQUIRE(ready.setVelocity);
    REQUIRE(ready.moveIntent == 0.f);

    EnemyAIDecision onCooldown = tickGrunt(scripts, kAttack, 1.f, 0.4f);
    REQUIRE_FALSE(onCooldown.meleeAttack);

    // Even when exiting back to Chase, the grunt branch still zeroes velocity
    // (unlike ranged/charger, which leave it untouched).
    EnemyAIDecision outOfRange = tickGrunt(scripts, kAttack, 5.f, 0.f);
    REQUIRE(outOfRange.state == kChase);
    REQUIRE(outOfRange.setVelocity);
    REQUIRE(outOfRange.moveIntent == 0.f);
    REQUIRE_FALSE(outOfRange.meleeAttack);
}

TEST_CASE("ds.enemy_ai Charger attack: telegraphs then lunges", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    EnemyAIDecision telegraph = tickCharger(scripts, kAttack, 2.f, 0.3f); // still winding up
    REQUIRE(telegraph.state == kAttack);
    REQUIRE(telegraph.setVelocity);
    REQUIRE(telegraph.moveIntent == 0.f);
    REQUIRE_FALSE(telegraph.lunge);

    EnemyAIDecision lunge = tickCharger(scripts, kAttack, 2.f, 0.f); // windup elapsed
    REQUIRE(lunge.state == kAttack);
    REQUIRE(lunge.lunge);
    REQUIRE(lunge.setVelocity);
    REQUIRE(lunge.moveIntent == 1.f);

    EnemyAIDecision outOfRange = tickCharger(scripts, kAttack, 10.f, 0.f); // beyond attackRange(2.5)
    REQUIRE(outOfRange.state == kChase);
    REQUIRE_FALSE(outOfRange.setVelocity);                                 // leaves velocity untouched, like ranged
}

TEST_CASE("ds.enemy_ai Ranged attack: retreats close, holds mid, fires on cooldown", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    // attackRange=12, half=6.
    EnemyAIDecision retreat = tickRanged(scripts, kAttack, 3.f, 0.f);
    REQUIRE(retreat.setVelocity);
    REQUIRE(retreat.moveIntent == -1.f);
    REQUIRE(retreat.fireProjectile);

    EnemyAIDecision hold = tickRanged(scripts, kAttack, 8.f, 0.f);
    REQUIRE(hold.setVelocity);
    REQUIRE(hold.moveIntent == 0.f);
    REQUIRE(hold.fireProjectile);

    EnemyAIDecision onCooldown = tickRanged(scripts, kAttack, 8.f, 0.4f);
    REQUIRE_FALSE(onCooldown.fireProjectile);

    EnemyAIDecision outOfRange = tickRanged(scripts, kAttack, 30.f, 0.f);
    REQUIRE(outOfRange.state == kChase);
    REQUIRE_FALSE(outOfRange.setVelocity);
    REQUIRE_FALSE(outOfRange.fireProjectile);
}

// Ports Engine::archetypeForWave's determinism via ScriptSystem::archetypeForWave.
TEST_CASE("ds.enemy_ai.archetype_for_wave is deterministic and escalates", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    // Wave 1: always Grunt regardless of spawn index.
    REQUIRE(scripts.archetypeForWave(1, 0) == kGrunt);
    REQUIRE(scripts.archetypeForWave(1, 5) == kGrunt);

    // Same (wave, spawn_index) always yields the same archetype.
    int a = scripts.archetypeForWave(4, 2);
    int b = scripts.archetypeForWave(4, 2);
    REQUIRE(a == b);

    // Wave 2: Chargers can appear, Ranged cannot yet (wave < 3).
    bool sawCharger2 = false;
    for (int i = 0; i < 6; ++i) {
        int archetype = scripts.archetypeForWave(2, i);
        REQUIRE(archetype != kRanged);
        if (archetype == kCharger)
            sawCharger2 = true;
    }
    REQUIRE(sawCharger2);

    // Wave 3+: Ranged can appear.
    bool sawRanged3 = false;
    for (int i = 0; i < 6; ++i) {
        if (scripts.archetypeForWave(3, i) == kRanged)
            sawRanged3 = true;
    }
    REQUIRE(sawRanged3);
}

TEST_CASE("enemy AI wrapper is graceful when enemy_ai.lua never loaded", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());
    // No loadFile() call: ds.enemy_ai doesn't exist.
    EnemyAIDecision d = scripts.enemyAITick(kGrunt, kIdle, 5.f, 0.f, 3.f, 1.5f, 15.f, 1.f, 0.f, 0.f);
    REQUIRE(d.state == kIdle);
    REQUIRE_FALSE(d.setVelocity);
    REQUIRE(scripts.archetypeForWave(5, 0) == kGrunt);
}
