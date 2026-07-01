#include "engine/MetaProgression.h"
#include "engine/SaveData.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>

using namespace ds;

// Exercises the pure meta-progression fold over SaveData (task 51). Links
// engine_math/engine_headers only: SaveData + MetaProgression are header-only
// POD/constexpr with no SDL3/Jolt state.

TEST_CASE("isUnlocked/setUnlock toggle bits independently and idempotently", "[meta]") {
    SaveData save{};
    REQUIRE_FALSE(isUnlocked(save, Unlock::AltFire));
    REQUIRE_FALSE(isUnlocked(save, Unlock::ExtraWeapon));

    setUnlock(save, Unlock::AltFire);
    REQUIRE(isUnlocked(save, Unlock::AltFire));
    REQUIRE_FALSE(isUnlocked(save, Unlock::ExtraWeapon)); // untouched

    // Idempotent: setting again leaves the bitset unchanged.
    const uint32_t before = save.unlockFlags;
    setUnlock(save, Unlock::AltFire);
    REQUIRE(save.unlockFlags == before);

    setUnlock(save, Unlock::HardMode);
    REQUIRE(isUnlocked(save, Unlock::AltFire));
    REQUIRE(isUnlocked(save, Unlock::HardMode));
}

TEST_CASE("applyRunResult keeps maxima on a worse run and accumulates kills", "[meta]") {
    SaveData prev{};
    prev.bestWave   = 5;
    prev.highScore  = 10000;
    prev.bestCombo  = 12;
    prev.totalKills = 100;
    prev.totalRuns  = 7;

    // A strictly-worse run: lower wave/score/combo, some kills.
    const RunResult worse{/*wave*/ 2, /*score*/ 500, /*kills*/ 9, /*bestCombo*/ 3};
    const SaveData after = applyRunResult(prev, worse);

    REQUIRE(after.bestWave == 5);      // preserved
    REQUIRE(after.highScore == 10000); // preserved
    REQUIRE(after.bestCombo == 12);    // preserved
    REQUIRE(after.totalKills == 109);  // accumulated: 100 + 9
    REQUIRE(after.totalRuns == 7);     // untouched by the fold
}

TEST_CASE("applyRunResult raises maxima on a better run", "[meta]") {
    SaveData prev{};
    prev.bestWave  = 3;
    prev.highScore = 1000;
    prev.bestCombo = 4;

    const RunResult better{/*wave*/ 6, /*score*/ 8000, /*kills*/ 50, /*bestCombo*/ 20};
    const SaveData after = applyRunResult(prev, better);

    REQUIRE(after.bestWave == 6);
    REQUIRE(after.highScore == 8000);
    REQUIRE(after.bestCombo == 20);
    REQUIRE(after.totalKills == 50);
}

TEST_CASE("applyRunResult clamps negative run figures to zero", "[meta]") {
    SaveData prev{};
    prev.totalKills = 5;

    const RunResult negative{/*wave*/ -3, /*score*/ -100, /*kills*/ -7, /*bestCombo*/ -1};
    const SaveData after = applyRunResult(prev, negative);

    REQUIRE(after.bestWave == 0);
    REQUIRE(after.highScore == 0);
    REQUIRE(after.bestCombo == 0);
    REQUIRE(after.totalKills == 5); // += max(kills, 0) == += 0
}

TEST_CASE("applyRunResult auto-unlocks at the deterministic wave thresholds", "[meta]") {
    // Below every threshold: no auto-unlocks.
    {
        const SaveData after = applyRunResult(SaveData{}, RunResult{/*wave*/ 1, 0, 0, 0});
        REQUIRE_FALSE(isUnlocked(after, Unlock::AltFire));
        REQUIRE_FALSE(isUnlocked(after, Unlock::ExtraWeapon));
        REQUIRE_FALSE(isUnlocked(after, Unlock::HardMode));
    }
    // Reaching the AltFire threshold unlocks only it.
    {
        const SaveData after = applyRunResult(SaveData{}, RunResult{/*wave*/ 2, 0, 0, 0});
        REQUIRE(isUnlocked(after, Unlock::AltFire));
        REQUIRE_FALSE(isUnlocked(after, Unlock::ExtraWeapon));
        REQUIRE_FALSE(isUnlocked(after, Unlock::HardMode));
    }
    // Clearing all 8 waves unlocks everything gated on best wave.
    {
        const SaveData after = applyRunResult(SaveData{}, RunResult{/*wave*/ 8, 0, 0, 0});
        REQUIRE(isUnlocked(after, Unlock::AltFire));
        REQUIRE(isUnlocked(after, Unlock::ExtraWeapon));
        REQUIRE(isUnlocked(after, Unlock::HardMode));
    }
}

TEST_CASE("auto-unlock is driven by the persisted best wave, not this run", "[meta]") {
    SaveData prev{};
    prev.bestWave = 8; // previously cleared

    // Even a wave-1 run keeps everything unlocked (bestWave stays 8).
    const SaveData after = applyRunResult(prev, RunResult{/*wave*/ 1, 0, 0, 0});
    REQUIRE(after.bestWave == 8);
    REQUIRE(isUnlocked(after, Unlock::HardMode));
}

TEST_CASE("startRun bumps only totalRuns", "[meta]") {
    SaveData prev{};
    prev.bestWave   = 4;
    prev.totalKills = 42;
    prev.totalRuns  = 2;

    const SaveData after = startRun(prev);
    REQUIRE(after.totalRuns == 3);
    // Everything else untouched.
    REQUIRE(after.bestWave == 4);
    REQUIRE(after.totalKills == 42);
    REQUIRE(after.highScore == 0);
}

TEST_CASE("full run loop round-trips through serialize/parse", "[meta]") {
    SaveData save{};
    save = startRun(save);                                           // totalRuns -> 1
    save = applyRunResult(save, RunResult{/*wave*/ 4, 3200, 27, 9}); // fold a run

    // Sanity on the folded values before persistence.
    REQUIRE(save.totalRuns == 1);
    REQUIRE(save.bestWave == 4);
    REQUIRE(save.highScore == 3200);
    REQUIRE(save.totalKills == 27);
    REQUIRE(save.bestCombo == 9);
    REQUIRE(isUnlocked(save, Unlock::AltFire));
    REQUIRE(isUnlocked(save, Unlock::ExtraWeapon));

    const std::optional<SaveData> out = parseSave(serializeSave(save));
    REQUIRE(out.has_value());
    REQUIRE(out->totalRuns == save.totalRuns);
    REQUIRE(out->bestWave == save.bestWave);
    REQUIRE(out->highScore == save.highScore);
    REQUIRE(out->totalKills == save.totalKills);
    REQUIRE(out->bestCombo == save.bestCombo);
    REQUIRE(out->unlockFlags == save.unlockFlags);
}

// =============================================================================
// Wave-D call-site-equivalence oracles (Phase 4 wiring de-risk).
//
// applyRunResult / startRun are NOT yet called by Engine.cpp; persistSave and
// startGame still fold run stats inline. These tests encode the EXACT inline
// formulas as oracles so the future Engine.cpp swap to the tested helpers is
// proven equivalent (a swap that changed behaviour would break one of these).
// =============================================================================

namespace {

// Verbatim transcription of the inline fold in Engine::persistSave
// (Engine.cpp:1264-1267): bestWave / highScore / bestCombo are running maxima,
// totalKills accumulates this run's kills. NOTE: the inline highScore uses the
// engine's already-reconciled m_highScore (recordHighScore ran first), which for
// a single run equals max(prevHighScore, thisRunScore) — the same value
// applyRunResult computes from RunResult.score. This oracle models that with the
// run's own score, matching the fold's observable result.
SaveData inlinePersistSaveFold(SaveData s, int wave, int score, int kills, int bestCombo) {
    s.bestWave  = std::max(s.bestWave, static_cast<uint32_t>(std::max(wave, 0)));
    s.highScore = std::max(s.highScore, static_cast<uint32_t>(std::max(score, 0)));
    s.bestCombo = std::max(s.bestCombo, static_cast<uint32_t>(std::max(bestCombo, 0)));
    s.totalKills += static_cast<uint32_t>(std::max(kills, 0));
    return s;
}

} // namespace

TEST_CASE("applyRunResult reproduces the inline persistSave fold exactly", "[meta][wireD]") {
    // A spread of starting states x run figures, including maxima that must be
    // preserved, kills that must accumulate, and negatives that must clamp.
    struct Case {
        SaveData prev;
        int wave, score, kills, bestCombo;
    };
    const Case cases[] = {
        {[] {
             SaveData s{};
             s.bestWave   = 5;
             s.highScore  = 10000;
             s.bestCombo  = 12;
             s.totalKills = 100;
             return s;
         }(),
         2, 500, 9, 3},
        {[] {
             SaveData s{};
             s.bestWave  = 3;
             s.highScore = 1000;
             s.bestCombo = 4;
             return s;
         }(),
         6, 8000, 50, 20},
        {[] {
             SaveData s{};
             s.totalKills = 5;
             return s;
         }(),
         -3, -100, -7, -1},
        {SaveData{}, 8, 42000, 300, 99},
    };

    for (const Case& c : cases) {
        const SaveData viaHelper = applyRunResult(c.prev, RunResult{c.wave, c.score, c.kills, c.bestCombo});
        const SaveData viaInline = inlinePersistSaveFold(c.prev, c.wave, c.score, c.kills, c.bestCombo);

        // The four folded stat fields must match the inline formula byte-for-byte.
        REQUIRE(viaHelper.bestWave == viaInline.bestWave);
        REQUIRE(viaHelper.highScore == viaInline.highScore);
        REQUIRE(viaHelper.bestCombo == viaInline.bestCombo);
        REQUIRE(viaHelper.totalKills == viaInline.totalKills);
        // The helper additionally applies auto-unlocks (a superset the inline
        // fold lacked) but never touches totalRuns — pin that it is untouched.
        REQUIRE(viaHelper.totalRuns == c.prev.totalRuns);
    }
}

TEST_CASE("startRun reproduces the inline startGame totalRuns bump", "[meta][wireD]") {
    // Engine::startGame does `++m_save.totalRuns;` (Engine.cpp:1206) and touches
    // nothing else on the save. startRun must match that exactly.
    SaveData prev{};
    prev.totalRuns  = 41;
    prev.bestWave   = 7;
    prev.highScore  = 9000;
    prev.totalKills = 123;

    SaveData inlineBump = prev;
    ++inlineBump.totalRuns; // the exact inline statement

    const SaveData viaHelper = startRun(prev);
    REQUIRE(viaHelper.totalRuns == inlineBump.totalRuns);
    REQUIRE(viaHelper.bestWave == inlineBump.bestWave);
    REQUIRE(viaHelper.highScore == inlineBump.highScore);
    REQUIRE(viaHelper.totalKills == inlineBump.totalKills);
    REQUIRE(viaHelper.bestCombo == inlineBump.bestCombo);
    REQUIRE(viaHelper.unlockFlags == inlineBump.unlockFlags);
}

TEST_CASE("difficulty 0-based SaveData <-> 1-based wave.lua round-trips", "[meta][wireD]") {
    // SaveData.difficulty is 0-based; ds.wave.set_difficulty is 1-based, so
    // feeding the save into the wave module is +1 and reading back is -1.
    // wave.lua defaults difficulty to 2 (NORMAL) == save value 1.
    REQUIRE(saveDifficultyToWave(0u) == 1); // EASY
    REQUIRE(saveDifficultyToWave(1u) == 2); // NORMAL (wave.lua default)
    REQUIRE(saveDifficultyToWave(2u) == 3); // HARD

    // Round-trips exactly for every valid save value.
    for (uint32_t d = 0; d < 3; ++d)
        REQUIRE(waveToSaveDifficulty(saveDifficultyToWave(d)) == d);

    // A default-constructed save (difficulty 0) maps to EASY's 1-based index...
    SaveData save{};
    REQUIRE(saveDifficultyToWave(save.difficulty) == 1);
    // ...and a wave index <= 1 clamps to save value 0 (never negative).
    REQUIRE(waveToSaveDifficulty(1) == 0u);
    REQUIRE(waveToSaveDifficulty(0) == 0u);
}
