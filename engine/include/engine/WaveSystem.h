#pragma once

#include <cstdint>

namespace ds {

// High-level game state. The Engine owns one of these and branches its
// run/update/render on it. Menu and the two end states pause gameplay and show
// a UI overlay; Playing runs the full simulation.
enum class GameState : uint8_t { Menu, Playing, Dead, Victory };

// Pure wave-progression + scoring logic, kept free of any engine/SDL/Jolt
// state so it can be unit-tested in isolation. The Engine owns one WaveState
// and drives it from update(); WaveSystem only computes counts, timers and
// score, never touches the world directly.
struct WaveState {
    int wave               = 0;     // current wave number (1-based once started)
    int aliveEnemies       = 0;     // enemies still alive in the active wave
    float intermission     = 0.f;   // seconds remaining before the next wave spawns
    bool intermissionArmed = false; // intermission countdown is running between waves
    bool spawnPending      = false; // set when a new wave's enemies must be spawned
    bool cleared           = false; // every wave finished (drives Victory)

    // Score tracking.
    int kills          = 0;
    int score          = 0;
    float timeSurvived = 0.f;
    int combo          = 0;   // consecutive kills inside the combo window
    int bestCombo      = 0;   // highest combo reached this run
    float comboTimer   = 0.f; // seconds left to chain the next kill
};

// Tuning constants for wave escalation, scoring and combos. Grouped so tests
// and the engine read the same numbers.
struct WaveConfig {
    int baseEnemies        = 3;   // enemies in wave 1
    int enemiesPerWave     = 2;   // additional enemies per subsequent wave
    int maxEnemiesPerWave  = 24;  // hard cap so late waves stay sane
    int maxWaves           = 8;   // clearing this many waves => Victory
    float intermissionTime = 3.f; // delay between a clear and the next spawn

    int killScore     = 100;      // base points per kill
    float comboWindow = 3.f;      // seconds to chain a kill into a combo
};

// Number of enemies to spawn for a given 1-based wave number, escalating by
// enemiesPerWave each wave and clamped to maxEnemiesPerWave.
int enemiesForWave(int wave, const WaveConfig& cfg);

// Points awarded for a single kill given the combo count *after* the kill is
// registered (combo == 1 for the first kill in a chain). The combo multiplier
// grows by 1x per chained kill: score = killScore * combo.
int scoreForKill(int combo, const WaveConfig& cfg);

// Register one enemy kill: advances kill count, combo (resetting the window),
// best combo and total score. Pure helper so the math is testable.
void registerKill(WaveState& state, const WaveConfig& cfg);

// Advance combo/intermission timers by dt. When the combo window lapses the
// combo resets to 0. Does not spawn or count enemies (the engine owns those).
void tickWave(WaveState& state, float dt);

// Begin the next wave: increments the wave number and flags spawnPending. The
// engine reads spawnPending, spawns enemiesForWave() enemies, sets aliveEnemies
// and clears the flag. If the previous wave was the final one, sets cleared.
void advanceWave(WaveState& state, const WaveConfig& cfg);

// Reset all wave + score state for a fresh run (used by restart).
void resetWave(WaveState& state);

} // namespace ds
