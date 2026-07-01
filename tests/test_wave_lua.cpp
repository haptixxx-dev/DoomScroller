#include "engine/ScriptSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace ds;

namespace {

// Loads the real, shipped assets/scripts/wave.lua (DS_ASSETS_DIR is injected
// by tests/CMakeLists.txt), same pattern as test_parry_lua.cpp.
void loadWaveScript(ScriptSystem& scripts) {
    REQUIRE(scripts.init());
    REQUIRE(scripts.loadFile(std::string(DS_ASSETS_DIR) + "/scripts/wave.lua"));
}

// set_difficulty / enemy_hp_scale have no C++ wrapper (runtime wiring is
// deferred), so drive them straight through the Lua VM and read the result
// back via a global. Returns the value ds.wave.enemy_hp_scale(wave) produced.
double hpScale(ScriptSystem& scripts, int wave) {
    REQUIRE(scripts.doString("__hp = ds.wave.enemy_hp_scale(" + std::to_string(wave) + ")"));
    return scripts.getGlobalNumber("__hp");
}

// Returns the clamped index ds.wave.set_difficulty(index) stored.
double setDifficulty(ScriptSystem& scripts, int index) {
    REQUIRE(scripts.doString("__diff = ds.wave.set_difficulty(" + std::to_string(index) + ")"));
    return scripts.getGlobalNumber("__diff");
}

} // namespace

// Ports tests/test_waves.cpp's WaveSystem.cpp cases onto the Lua port, driven
// through ScriptSystem::wave*() exactly as Engine.cpp now does.
TEST_CASE("waveEnemiesForWave escalates and clamps", "[scripting][waves]") {
    ScriptSystem scripts;
    loadWaveScript(scripts); // defaults: base 3, +2/wave, cap 24

    REQUIRE(scripts.waveEnemiesForWave(1) == 3);
    REQUIRE(scripts.waveEnemiesForWave(2) == 5);
    REQUIRE(scripts.waveEnemiesForWave(3) == 7);
    REQUIRE(scripts.waveEnemiesForWave(4) == 9);

    // Clamp at max_enemies_per_wave.
    REQUIRE(scripts.waveEnemiesForWave(100) == 24);

    // Out-of-range waves spawn nothing.
    REQUIRE(scripts.waveEnemiesForWave(0) == 0);
    REQUIRE(scripts.waveEnemiesForWave(-1) == 0);
}

TEST_CASE("waveEnemiesForWave honours custom tuning", "[scripting][waves]") {
    ScriptSystem scripts;
    loadWaveScript(scripts);
    REQUIRE(scripts.doString("ds.wave.config.base_enemies = 5\n"
                             "ds.wave.config.enemies_per_wave = 3\n"
                             "ds.wave.config.max_enemies_per_wave = 10"));

    REQUIRE(scripts.waveEnemiesForWave(1) == 5);
    REQUIRE(scripts.waveEnemiesForWave(2) == 8);
    REQUIRE(scripts.waveEnemiesForWave(3) == 10); // would be 11, clamped
    REQUIRE(scripts.waveEnemiesForWave(4) == 10);
}

TEST_CASE("registerKill chains combos inside the window", "[scripting][waves][score]") {
    ScriptSystem scripts;
    loadWaveScript(scripts);
    REQUIRE(scripts.doString("ds.wave.state.alive_enemies = 3"));

    scripts.waveRegisterKill(); // first kill: combo 1, +100
    WaveState s = scripts.readWaveState();
    REQUIRE(s.kills == 1);
    REQUIRE(s.combo == 1);
    REQUIRE(s.bestCombo == 1);
    REQUIRE(s.score == 100);
    REQUIRE(s.aliveEnemies == 2);
    REQUIRE(s.comboTimer == Catch::Approx(3.f)); // default combo_window

    scripts.waveRegisterKill();                  // chained: combo 2, +200
    s = scripts.readWaveState();
    REQUIRE(s.kills == 2);
    REQUIRE(s.combo == 2);
    REQUIRE(s.bestCombo == 2);
    REQUIRE(s.score == 300);
    REQUIRE(s.aliveEnemies == 1);

    scripts.waveRegisterKill(); // chained: combo 3, +300
    s = scripts.readWaveState();
    REQUIRE(s.combo == 3);
    REQUIRE(s.score == 600);
    REQUIRE(s.aliveEnemies == 0);
}

TEST_CASE("combo resets after the window lapses", "[scripting][waves][score]") {
    ScriptSystem scripts;
    loadWaveScript(scripts);
    REQUIRE(scripts.doString("ds.wave.state.alive_enemies = 10"));

    scripts.waveRegisterKill();
    REQUIRE(scripts.readWaveState().combo == 1);

    // Let the combo window (3s default) expire.
    scripts.waveTick(3.1f);
    WaveState s = scripts.readWaveState();
    REQUIRE(s.combo == 0);
    REQUIRE(s.comboTimer == Catch::Approx(0.f));

    // Next kill starts a fresh chain at combo 1 (not 2).
    scripts.waveRegisterKill();
    s = scripts.readWaveState();
    REQUIRE(s.combo == 1);
    REQUIRE(s.bestCombo == 1);
    REQUIRE(s.score == 200); // 100 + 100
}

TEST_CASE("waveTick accrues survival time and counts down intermission", "[scripting][waves]") {
    ScriptSystem scripts;
    loadWaveScript(scripts);
    REQUIRE(scripts.doString("ds.wave.state.intermission = 3"));

    scripts.waveTick(1.f);
    WaveState s = scripts.readWaveState();
    REQUIRE(s.timeSurvived == Catch::Approx(1.f));
    REQUIRE(s.intermission == Catch::Approx(2.f));

    scripts.waveTick(5.f);
    s = scripts.readWaveState();
    REQUIRE(s.intermission == Catch::Approx(0.f)); // clamped, not negative
    REQUIRE(s.timeSurvived == Catch::Approx(6.f));
}

TEST_CASE("waveAdvance increments wave and flags spawn until the cap", "[scripting][waves]") {
    ScriptSystem scripts;
    loadWaveScript(scripts);
    REQUIRE(scripts.doString("ds.wave.config.max_waves = 3"));

    scripts.waveAdvance();
    WaveState s = scripts.readWaveState();
    REQUIRE(s.wave == 1);
    REQUIRE(s.spawnPending);
    REQUIRE_FALSE(s.cleared);
    REQUIRE(scripts.doString("ds.wave.state.spawn_pending = false"));

    scripts.waveAdvance();
    REQUIRE(scripts.readWaveState().wave == 2);
    REQUIRE(scripts.doString("ds.wave.state.spawn_pending = false"));

    scripts.waveAdvance();
    REQUIRE(scripts.readWaveState().wave == 3);
    REQUIRE(scripts.doString("ds.wave.state.spawn_pending = false"));

    // Beyond the final wave: no increment, run is cleared (Victory).
    scripts.waveAdvance();
    s = scripts.readWaveState();
    REQUIRE(s.wave == 3);
    REQUIRE(s.cleared);
    REQUIRE_FALSE(s.spawnPending);
}

TEST_CASE("waveReset restores a fresh run", "[scripting][waves]") {
    ScriptSystem scripts;
    loadWaveScript(scripts);
    REQUIRE(scripts.doString("ds.wave.state.wave = 5\n"
                             "ds.wave.state.kills = 20\n"
                             "ds.wave.state.score = 4200\n"
                             "ds.wave.state.combo = 7\n"
                             "ds.wave.state.time_survived = 99"));

    scripts.waveReset();
    WaveState s = scripts.readWaveState();
    REQUIRE(s.wave == 0);
    REQUIRE(s.kills == 0);
    REQUIRE(s.score == 0);
    REQUIRE(s.combo == 0);
    REQUIRE(s.timeSurvived == Catch::Approx(0.f));
    REQUIRE_FALSE(s.cleared);
}

// ds.wave.advance() folds in the "intermission_armed = false" reset that used
// to be a separate manual assignment in Engine::updateWaves() before calling
// advanceWave() — verify that fold-in still behaves correctly.
TEST_CASE("waveAdvance clears intermission_armed", "[scripting][waves]") {
    ScriptSystem scripts;
    loadWaveScript(scripts);
    REQUIRE(scripts.doString("ds.wave.state.intermission_armed = true"));

    scripts.waveAdvance();
    REQUIRE_FALSE(scripts.readWaveState().intermissionArmed);
}

TEST_CASE("waveArmIntermission sets the countdown from config", "[scripting][waves]") {
    ScriptSystem scripts;
    loadWaveScript(scripts);

    scripts.waveArmIntermission();
    WaveState s = scripts.readWaveState();
    REQUIRE(s.intermission == Catch::Approx(3.f)); // default intermission_time
    REQUIRE(s.intermissionArmed);
}

TEST_CASE("waveSetAliveEnemies and waveMarkSpawned write through", "[scripting][waves]") {
    ScriptSystem scripts;
    loadWaveScript(scripts);

    scripts.waveSetAliveEnemies(7);
    REQUIRE(scripts.readWaveState().aliveEnemies == 7);

    REQUIRE(scripts.doString("ds.wave.state.spawn_pending = true"));
    scripts.waveMarkSpawned(5);
    WaveState s = scripts.readWaveState();
    REQUIRE(s.aliveEnemies == 5);
    REQUIRE_FALSE(s.spawnPending);
}

TEST_CASE("wave wrappers are graceful when wave.lua never loaded", "[scripting][waves]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());
    // No loadFile() call: ds.wave doesn't exist.
    scripts.waveReset();
    scripts.waveTick(0.016f);
    scripts.waveRegisterKill();
    scripts.waveAdvance();
    scripts.waveArmIntermission();
    scripts.waveSetAliveEnemies(3);
    scripts.waveMarkSpawned(3);
    REQUIRE(scripts.waveEnemiesForWave(1) == 0);
    WaveState s = scripts.readWaveState();
    REQUIRE(s.wave == 0);
    REQUIRE(s.score == 0);
}

// -----------------------------------------------------------------------------
// Task 53: difficulty curve. set_difficulty clamps to the table; the spawn
// count and HP scale fold in the selected difficulty's scalars.
// -----------------------------------------------------------------------------

TEST_CASE("set_difficulty clamps to the difficulty table", "[scripting][waves][difficulty]") {
    ScriptSystem scripts;
    loadWaveScript(scripts);

    REQUIRE(setDifficulty(scripts, 1) == 1.0); // EASY
    REQUIRE(setDifficulty(scripts, 3) == 3.0); // HARD

    // Out-of-range requests clamp to [1, #difficulties] (3 entries).
    REQUIRE(setDifficulty(scripts, 0) == 1.0);
    REQUIRE(setDifficulty(scripts, -5) == 1.0);
    REQUIRE(setDifficulty(scripts, 99) == 3.0);
}

TEST_CASE("enemies_for_wave folds in the difficulty scalars", "[scripting][waves][difficulty]") {
    ScriptSystem scripts;
    loadWaveScript(scripts); // defaults: base 3, +2/wave, cap 24, difficulty NORMAL

    // NORMAL (index 2) is the neutral baseline: unchanged counts.
    setDifficulty(scripts, 2);
    REQUIRE(scripts.waveEnemiesForWave(1) == 3); // floor(3*1.0)+0
    REQUIRE(scripts.waveEnemiesForWave(2) == 5); // floor(5*1.0)+0

    // EASY (index 1): enemy_mult 0.75, count_bonus 0.
    setDifficulty(scripts, 1);
    REQUIRE(scripts.waveEnemiesForWave(1) == 2); // floor(3*0.75)=2
    REQUIRE(scripts.waveEnemiesForWave(2) == 3); // floor(5*0.75)=3
    REQUIRE(scripts.waveEnemiesForWave(5) == 8); // base 11 -> floor(8.25)=8

    // HARD (index 3): enemy_mult 1.25, count_bonus +1.
    setDifficulty(scripts, 3);
    REQUIRE(scripts.waveEnemiesForWave(1) == 4); // floor(3*1.25)+1 = 3+1
    REQUIRE(scripts.waveEnemiesForWave(2) == 7); // floor(5*1.25)+1 = 6+1

    // Cap still applies after scaling: a huge wave clamps to max_enemies_per_wave.
    REQUIRE(scripts.waveEnemiesForWave(100) == 24);

    // Out-of-range waves still spawn nothing regardless of difficulty.
    REQUIRE(scripts.waveEnemiesForWave(0) == 0);
}

TEST_CASE("enemy_hp_scale is monotonic and folds hp_mult + clamps low waves", "[scripting][waves][difficulty]") {
    ScriptSystem scripts;
    loadWaveScript(scripts);

    setDifficulty(scripts, 2); // NORMAL: hp_mult 1.0
    REQUIRE(hpScale(scripts, 1) == Catch::Approx(1.0));
    REQUIRE(hpScale(scripts, 2) == Catch::Approx(1.08));
    REQUIRE(hpScale(scripts, 6) == Catch::Approx(1.4)); // 1 + 5*0.08

    // Monotonic non-decreasing across waves.
    double prev = 0.0;
    for (int w = 1; w <= 12; ++w) {
        const double s = hpScale(scripts, w);
        REQUIRE(s >= prev);
        prev = s;
    }

    // Waves below 1 clamp to wave 1 (never less than the wave-1 scale).
    REQUIRE(hpScale(scripts, 0) == Catch::Approx(hpScale(scripts, 1)));
    REQUIRE(hpScale(scripts, -3) == Catch::Approx(hpScale(scripts, 1)));

    // hp_mult scales the whole curve: HARD > NORMAL > EASY at the same wave.
    const double normal6 = hpScale(scripts, 6);
    setDifficulty(scripts, 3); // HARD: hp_mult 1.25
    const double hard6 = hpScale(scripts, 6);
    setDifficulty(scripts, 1); // EASY: hp_mult 0.85
    const double easy6 = hpScale(scripts, 6);
    REQUIRE(hard6 > normal6);
    REQUIRE(normal6 > easy6);
    REQUIRE(hard6 == Catch::Approx(1.4 * 1.25));
    REQUIRE(easy6 == Catch::Approx(1.4 * 0.85));
}
