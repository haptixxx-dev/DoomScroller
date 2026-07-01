#pragma once

#include "engine/SaveData.h"

#include <algorithm>
#include <cstdint>

// =============================================================================
// Meta-progression: naming + fold rules over SaveData (Phase 4 Wave D task 51)
// =============================================================================
//
// SaveData is the raw persistent blob (SaveData.h). This header gives its
// `unlockFlags` bitset meaning (the Unlock enum) and centralises the pure
// end-of-run fold that used to live inline in Engine::persistSave, plus the
// deterministic auto-unlock rules. Everything here is constexpr and depends
// only on SaveData.h + the stdlib, so it links into the pure math/logic test
// target with no SDL3/Jolt/EnTT runtime state.
//
// The fold reproduces Engine::persistSave exactly: bestWave / highScore /
// bestCombo are running maxima, totalKills accumulates this run's kills
// (clamped to >= 0). totalRuns is NOT bumped here — a run is *started* via
// startRun(), not ended — matching Engine bumping totalRuns at startGame.
// =============================================================================

namespace ds {

// Named bits of SaveData::unlockFlags. Content gated behind meta-progression.
// Values are explicit powers of two so the on-disk bitset stays stable.
enum class Unlock : uint32_t {
    AltFire     = 1u << 0, // right-mouse alt-fire modes
    ExtraWeapon = 1u << 1, // a second unlockable weapon slot
    HardMode    = 1u << 2, // hard difficulty selectable in the menu
    ArenaTheme  = 1u << 3, // alternate arena visual theme
};

// True if `flag` is set in the save's unlock bitset.
constexpr bool isUnlocked(const SaveData& save, Unlock flag) {
    return (save.unlockFlags & static_cast<uint32_t>(flag)) != 0u;
}

// Set `flag` in the save's unlock bitset (idempotent).
constexpr void setUnlock(SaveData& save, Unlock flag) {
    save.unlockFlags |= static_cast<uint32_t>(flag);
}

// The tunable, deterministic thresholds that gate auto-unlocks. Reaching a
// best wave at or beyond a threshold sets the associated Unlock.
inline constexpr uint32_t kAltFireBestWave     = 2u; // survive to wave 2 -> alt-fire
inline constexpr uint32_t kExtraWeaponBestWave = 4u; // survive to wave 4 -> extra weapon
inline constexpr uint32_t kHardModeBestWave    = 8u; // clear all 8 waves -> hard mode

// The outcome of a single completed run, as the numbers the fold cares about.
struct RunResult {
    int wave      = 0; // furthest wave reached this run
    int score     = 0; // final score this run
    int kills     = 0; // kills this run
    int bestCombo = 0; // best combo reached this run
};

// Apply deterministic auto-unlock rules against the (already folded) maxima.
// Pure, idempotent: bits only ever turn on, never off.
constexpr SaveData applyAutoUnlocks(SaveData save) {
    if (save.bestWave >= kAltFireBestWave) {
        setUnlock(save, Unlock::AltFire);
    }
    if (save.bestWave >= kExtraWeaponBestWave) {
        setUnlock(save, Unlock::ExtraWeapon);
    }
    if (save.bestWave >= kHardModeBestWave) {
        setUnlock(save, Unlock::HardMode);
    }
    return save;
}

// Fold a finished run into the save, reproducing Engine::persistSave: maxima
// for bestWave / highScore / bestCombo, accumulate clamped kills into
// totalKills. Negative run figures are clamped to 0 before use. Then apply the
// deterministic auto-unlock rules. totalRuns is untouched (see startRun).
constexpr SaveData applyRunResult(SaveData prev, const RunResult& run) {
    prev.bestWave  = std::max(prev.bestWave, static_cast<uint32_t>(std::max(run.wave, 0)));
    prev.highScore = std::max(prev.highScore, static_cast<uint32_t>(std::max(run.score, 0)));
    prev.bestCombo = std::max(prev.bestCombo, static_cast<uint32_t>(std::max(run.bestCombo, 0)));
    prev.totalKills += static_cast<uint32_t>(std::max(run.kills, 0));
    return applyAutoUnlocks(prev);
}

// Register the start of a fresh run: bumps the lifetime run counter only.
constexpr SaveData startRun(SaveData prev) {
    ++prev.totalRuns;
    return prev;
}

// -----------------------------------------------------------------------------
// Difficulty index mapping (Phase 4 Wave D wiring de-risk).
//
// SaveData.difficulty is 0-based (SaveData.h:101 — "0-based; wave.lua is
// 1-based"). The Lua side, ds.wave.set_difficulty(index), is 1-based (wave.lua:
// 1=EASY, 2=NORMAL, 3=HARD; default 2). So feeding the persisted difficulty into
// the wave module needs a +1 conversion, and reading a chosen wave-index back
// into the save needs a -1. These two pure helpers name that off-by-one so the
// future Engine.cpp call-site swap can't get the direction wrong, and round-trip
// exactly: waveToSaveDifficulty(saveDifficultyToWave(d)) == d.
// -----------------------------------------------------------------------------

// Convert a 0-based SaveData.difficulty into the 1-based index wave.lua's
// ds.wave.set_difficulty expects (adds 1).
constexpr int saveDifficultyToWave(uint32_t saveDifficulty) {
    return static_cast<int>(saveDifficulty) + 1;
}

// Convert a 1-based wave.lua difficulty index back into a 0-based
// SaveData.difficulty (subtracts 1; a <1 index clamps to 0, matching the fact
// that the save can never hold a negative field).
constexpr uint32_t waveToSaveDifficulty(int waveDifficulty) {
    return waveDifficulty <= 1 ? 0u : static_cast<uint32_t>(waveDifficulty - 1);
}

} // namespace ds
