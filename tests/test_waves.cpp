#include "engine/WaveSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ds;

TEST_CASE("enemiesForWave escalates and clamps", "[waves]") {
    WaveConfig cfg; // base 3, +2/wave, cap 24

    REQUIRE(enemiesForWave(1, cfg) == 3);
    REQUIRE(enemiesForWave(2, cfg) == 5);
    REQUIRE(enemiesForWave(3, cfg) == 7);
    REQUIRE(enemiesForWave(4, cfg) == 9);

    // Clamp at maxEnemiesPerWave.
    REQUIRE(enemiesForWave(100, cfg) == cfg.maxEnemiesPerWave);

    // Out-of-range waves spawn nothing.
    REQUIRE(enemiesForWave(0, cfg) == 0);
    REQUIRE(enemiesForWave(-1, cfg) == 0);
}

TEST_CASE("enemiesForWave honours custom tuning", "[waves]") {
    WaveConfig cfg;
    cfg.baseEnemies       = 5;
    cfg.enemiesPerWave    = 3;
    cfg.maxEnemiesPerWave = 10;

    REQUIRE(enemiesForWave(1, cfg) == 5);
    REQUIRE(enemiesForWave(2, cfg) == 8);
    REQUIRE(enemiesForWave(3, cfg) == 10); // would be 11, clamped
    REQUIRE(enemiesForWave(4, cfg) == 10);
}

TEST_CASE("scoreForKill scales with combo multiplier", "[waves][score]") {
    WaveConfig cfg;                       // killScore 100

    REQUIRE(scoreForKill(0, cfg) == 100); // treated as 1x
    REQUIRE(scoreForKill(1, cfg) == 100);
    REQUIRE(scoreForKill(2, cfg) == 200);
    REQUIRE(scoreForKill(5, cfg) == 500);
}

TEST_CASE("registerKill chains combos inside the window", "[waves][score]") {
    WaveConfig cfg;
    WaveState s;
    s.aliveEnemies = 3;

    registerKill(s, cfg); // first kill: combo 1, +100
    REQUIRE(s.kills == 1);
    REQUIRE(s.combo == 1);
    REQUIRE(s.bestCombo == 1);
    REQUIRE(s.score == 100);
    REQUIRE(s.aliveEnemies == 2);
    REQUIRE(s.comboTimer == Catch::Approx(cfg.comboWindow));

    registerKill(s, cfg); // chained: combo 2, +200
    REQUIRE(s.kills == 2);
    REQUIRE(s.combo == 2);
    REQUIRE(s.bestCombo == 2);
    REQUIRE(s.score == 300);
    REQUIRE(s.aliveEnemies == 1);

    registerKill(s, cfg); // chained: combo 3, +300
    REQUIRE(s.combo == 3);
    REQUIRE(s.score == 600);
    REQUIRE(s.aliveEnemies == 0);
}

TEST_CASE("combo resets after the window lapses", "[waves][score]") {
    WaveConfig cfg;
    WaveState s;
    s.aliveEnemies = 10;

    registerKill(s, cfg);
    REQUIRE(s.combo == 1);

    // Let the combo window expire.
    tickWave(s, cfg.comboWindow + 0.1f);
    REQUIRE(s.combo == 0);
    REQUIRE(s.comboTimer == Catch::Approx(0.f));

    // Next kill starts a fresh chain at combo 1 (not 2).
    registerKill(s, cfg);
    REQUIRE(s.combo == 1);
    REQUIRE(s.bestCombo == 1);
    REQUIRE(s.score == 200); // 100 + 100
}

TEST_CASE("tickWave accrues survival time and counts down intermission", "[waves]") {
    WaveState s;
    s.intermission = 3.f;

    tickWave(s, 1.f);
    REQUIRE(s.timeSurvived == Catch::Approx(1.f));
    REQUIRE(s.intermission == Catch::Approx(2.f));

    tickWave(s, 5.f);
    REQUIRE(s.intermission == Catch::Approx(0.f)); // clamped, not negative
    REQUIRE(s.timeSurvived == Catch::Approx(6.f));
}

TEST_CASE("advanceWave increments wave and flags spawn until the cap", "[waves]") {
    WaveConfig cfg;
    cfg.maxWaves = 3;
    WaveState s;

    advanceWave(s, cfg);
    REQUIRE(s.wave == 1);
    REQUIRE(s.spawnPending);
    REQUIRE_FALSE(s.cleared);
    s.spawnPending = false;

    advanceWave(s, cfg);
    REQUIRE(s.wave == 2);
    s.spawnPending = false;

    advanceWave(s, cfg);
    REQUIRE(s.wave == 3);
    s.spawnPending = false;

    // Beyond the final wave: no increment, run is cleared (Victory).
    advanceWave(s, cfg);
    REQUIRE(s.wave == 3);
    REQUIRE(s.cleared);
    REQUIRE_FALSE(s.spawnPending);
}

TEST_CASE("resetWave restores a fresh run", "[waves]") {
    WaveState s;
    s.wave         = 5;
    s.kills        = 20;
    s.score        = 4200;
    s.combo        = 7;
    s.timeSurvived = 99.f;

    resetWave(s);
    REQUIRE(s.wave == 0);
    REQUIRE(s.kills == 0);
    REQUIRE(s.score == 0);
    REQUIRE(s.combo == 0);
    REQUIRE(s.timeSurvived == Catch::Approx(0.f));
    REQUIRE_FALSE(s.cleared);
}
