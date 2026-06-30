#pragma once

#include <cstdint>

namespace ds {

// High-level game state. The Engine owns one of these and branches its
// run/update/render on it. Menu and the two end states pause gameplay and show
// a UI overlay; Playing runs the full simulation.
enum class GameState : uint8_t { Menu, Playing, Dead, Victory };

// Wave-progression + scoring state. The logic that used to live in
// WaveSystem.cpp (enemiesForWave/scoreForKill/registerKill/tickWave/
// advanceWave/resetWave) is now Lua-side (assets/scripts/wave.lua, module
// ds.wave); this struct is a read-back cache the Engine refreshes from
// ScriptSystem::readWaveState() after every mutating ds.wave call, kept here
// (rather than moved into ScriptSystem.h) because Engine.h/HUD/save code
// reads m_wave.* fields directly in many places.
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

// Tuning constants for wave escalation, scoring and combos. Lua's ds.wave.config
// is the live source of truth (assets/scripts/wave.lua); this struct is no
// longer read by Engine.cpp's wave logic (Lua reads its own copy internally)
// but stays for any external code that still wants the defaults' shape.
struct WaveConfig {
    int baseEnemies        = 3;   // enemies in wave 1
    int enemiesPerWave     = 2;   // additional enemies per subsequent wave
    int maxEnemiesPerWave  = 24;  // hard cap so late waves stay sane
    int maxWaves           = 8;   // clearing this many waves => Victory
    float intermissionTime = 3.f; // delay between a clear and the next spawn

    int killScore     = 100;      // base points per kill
    float comboWindow = 3.f;      // seconds to chain a kill into a combo
};

} // namespace ds
