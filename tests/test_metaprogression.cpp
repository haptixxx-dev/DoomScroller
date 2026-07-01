#include "engine/MetaProgression.h"
#include "engine/SaveData.h"

#include <catch2/catch_test_macros.hpp>
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
