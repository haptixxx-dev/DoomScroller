#include "engine/WaveSystem.h"

#include <algorithm>

namespace ds {

int enemiesForWave(int wave, const WaveConfig& cfg) {
    if (wave < 1)
        return 0;
    int count = cfg.baseEnemies + (wave - 1) * cfg.enemiesPerWave;
    return std::min(count, cfg.maxEnemiesPerWave);
}

int scoreForKill(int combo, const WaveConfig& cfg) {
    int mult = combo > 0 ? combo : 1;
    return cfg.killScore * mult;
}

void registerKill(WaveState& state, const WaveConfig& cfg) {
    ++state.kills;
    // Chain the combo if the window is still open, otherwise start fresh at 1.
    state.combo      = state.comboTimer > 0.f ? state.combo + 1 : 1;
    state.comboTimer = cfg.comboWindow;
    state.bestCombo  = std::max(state.bestCombo, state.combo);
    state.score += scoreForKill(state.combo, cfg);
    if (state.aliveEnemies > 0)
        --state.aliveEnemies;
}

void tickWave(WaveState& state, float dt) {
    state.timeSurvived += dt;

    if (state.comboTimer > 0.f) {
        state.comboTimer -= dt;
        if (state.comboTimer <= 0.f) {
            state.comboTimer = 0.f;
            state.combo      = 0;
        }
    }

    if (state.intermission > 0.f) {
        state.intermission -= dt;
        if (state.intermission < 0.f)
            state.intermission = 0.f;
    }
}

void advanceWave(WaveState& state, const WaveConfig& cfg) {
    if (state.wave >= cfg.maxWaves) {
        state.cleared = true;
        return;
    }
    ++state.wave;
    state.spawnPending = true;
}

void resetWave(WaveState& state) {
    state = WaveState{};
}

} // namespace ds
