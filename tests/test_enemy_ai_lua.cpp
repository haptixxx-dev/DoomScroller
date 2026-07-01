#include "engine/ScriptSystem.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace ds;

namespace {

constexpr int kGrunt   = 0;
constexpr int kCharger = 1;
constexpr int kRanged  = 2;
constexpr int kBrute   = 3;
constexpr int kSpitter = 4;

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

// Brute uses the melee FSM branch (EnemyArchetype.h Brute defaults: moveSpeed 2,
// attackRange 2, detectionRange 16, attackInterval 1.8, no charge/projectile).
EnemyAIDecision tickBrute(ScriptSystem& scripts, int state, float dist, float cooldown) {
    return scripts.enemyAITick(kBrute, state, dist, cooldown, 2.f, 2.f, 16.f, 1.8f, 0.f, 0.f);
}

// Spitter uses the ranged FSM branch (Spitter defaults: moveSpeed 4,
// attackRange 11, detectionRange 22, attackInterval 0.6, projectileSpeed 22).
EnemyAIDecision tickSpitter(ScriptSystem& scripts, int state, float dist, float cooldown) {
    return scripts.enemyAITick(kSpitter, state, dist, cooldown, 4.f, 11.f, 22.f, 0.6f, 0.f, 0.f);
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

    // Pin the EXACT per-spawn_index mapping for waves 2-3 to the pre-task-54
    // formula so the %3->%5 restructure cannot silently shift it: for wave < 4,
    // sel = (wave + i) % 3; ranged iff (wave>=3 && sel==2), else charger iff
    // sel==1, else grunt. Any off-by-one in the selector fails here.
    for (int wave = 2; wave <= 3; ++wave) {
        for (int i = 0; i < 12; ++i) {
            const int sel = (wave + i) % 3;
            int expected  = kGrunt;
            if (wave >= 3 && sel == 2)
                expected = kRanged;
            else if (sel == 1)
                expected = kCharger;
            INFO("wave=" << wave << " spawn_index=" << i);
            REQUIRE(scripts.archetypeForWave(wave, i) == expected);
        }
    }
}

// Task 54: Brute reuses the melee FSM, Spitter reuses the ranged FSM.
TEST_CASE("ds.enemy_ai Brute follows the melee FSM like a Grunt", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    // Enters attack range without arming a charger windup (melee, not charger).
    EnemyAIDecision enter = tickBrute(scripts, kChase, 1.5f, 0.f); // within attackRange(2)
    REQUIRE(enter.state == kAttack);
    REQUIRE_FALSE(enter.armWindup);
    REQUIRE_FALSE(enter.lunge);

    // In range with cooldown elapsed: melee attack, velocity zeroed, no projectile.
    EnemyAIDecision melee = tickBrute(scripts, kAttack, 1.5f, 0.f);
    REQUIRE(melee.meleeAttack);
    REQUIRE_FALSE(melee.fireProjectile);
    REQUIRE(melee.setVelocity);
    REQUIRE(melee.moveIntent == 0.f);

    // Out of range from Attack: back to Chase, still zeroing velocity (grunt path).
    EnemyAIDecision out = tickBrute(scripts, kAttack, 5.f, 0.f);
    REQUIRE(out.state == kChase);
    REQUIRE(out.setVelocity);
    REQUIRE_FALSE(out.meleeAttack);
}

TEST_CASE("ds.enemy_ai Spitter follows the ranged FSM and fires projectiles", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    // attackRange=11, half=5.5. Close in: retreat and fire.
    EnemyAIDecision retreat = tickSpitter(scripts, kAttack, 3.f, 0.f);
    REQUIRE(retreat.setVelocity);
    REQUIRE(retreat.moveIntent == -1.f);
    REQUIRE(retreat.fireProjectile);
    REQUIRE_FALSE(retreat.meleeAttack);

    // Mid: hold and fire on cooldown-elapsed.
    EnemyAIDecision hold = tickSpitter(scripts, kAttack, 8.f, 0.f);
    REQUIRE(hold.moveIntent == 0.f);
    REQUIRE(hold.fireProjectile);

    // On cooldown: no fire.
    EnemyAIDecision onCd = tickSpitter(scripts, kAttack, 8.f, 0.3f);
    REQUIRE_FALSE(onCd.fireProjectile);

    // Out of range: back to Chase, leaves velocity untouched (ranged path).
    EnemyAIDecision out = tickSpitter(scripts, kAttack, 30.f, 0.f);
    REQUIRE(out.state == kChase);
    REQUIRE_FALSE(out.setVelocity);
    REQUIRE_FALSE(out.fireProjectile);
}

// Task 54: archetype_for_wave gates Brute (wave >= 4) and Spitter (wave >= 5).
TEST_CASE("ds.enemy_ai.archetype_for_wave gates Brute and Spitter by wave", "[scripting][enemy_ai]") {
    ScriptSystem scripts;
    loadEnemyAiScript(scripts);

    // Determinism holds for the new families too.
    REQUIRE(scripts.archetypeForWave(5, 4) == scripts.archetypeForWave(5, 4));

    // Waves 1-3 never field Brute or Spitter.
    for (int w = 1; w <= 3; ++w) {
        for (int i = 0; i < 12; ++i) {
            const int a = scripts.archetypeForWave(w, i);
            REQUIRE(a != kBrute);
            REQUIRE(a != kSpitter);
        }
    }

    // Wave 4: Brute can appear, Spitter cannot yet.
    bool sawBrute4 = false;
    for (int i = 0; i < 12; ++i) {
        const int a = scripts.archetypeForWave(4, i);
        REQUIRE(a != kSpitter);
        if (a == kBrute)
            sawBrute4 = true;
    }
    REQUIRE(sawBrute4);

    // Wave 5+: Spitter can appear.
    bool sawSpitter5 = false;
    for (int i = 0; i < 12; ++i) {
        if (scripts.archetypeForWave(5, i) == kSpitter)
            sawSpitter5 = true;
    }
    REQUIRE(sawSpitter5);
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
