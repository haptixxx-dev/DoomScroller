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
