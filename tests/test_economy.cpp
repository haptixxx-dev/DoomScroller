#include "engine/MetaProgression.h"
#include "engine/SaveData.h"
#include "engine/WeaponEconomy.h"
#include "engine/WeaponUpgrade.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using namespace ds;

// Exercises the pure weapon-economy layer (task 52). Header-only, POD-only, so
// it links engine_math/engine_headers with no SDL3/Jolt runtime state.

TEST_CASE("currencyForRun is deterministic, additive and floors style", "[economy]") {
    const CurrencyRates rates{}; // perKill 2, scoreDivisor 50, perCombo 1, styleDivisor 10

    const RunEconomyStats a{/*kills*/ 10, /*score*/ 500, /*bestCombo*/ 4, /*style*/ 35.f};
    const uint32_t ca = currencyForRun(a, rates);
    // 10*2 + 500/50 + 4*1 + floor(35)/10 = 20 + 10 + 4 + 3 = 37.
    REQUIRE(ca == 37u);

    // Determinism: same inputs -> same output.
    REQUIRE(currencyForRun(a, rates) == ca);

    // Style is floored before dividing: 35.9 still contributes floor(35)/10 = 3.
    const RunEconomyStats aFrac{10, 500, 4, 35.9f};
    REQUIRE(currencyForRun(aFrac, rates) == ca);

    // Additivity over the integer figures (score/style contributions add
    // linearly here because they land on exact divisor multiples).
    const RunEconomyStats b{/*kills*/ 5, /*score*/ 250, /*bestCombo*/ 1, /*style*/ 20.f};
    const RunEconomyStats sum{15, 750, 5, 55.f};
    REQUIRE(currencyForRun(sum, rates) == currencyForRun(a, rates) + currencyForRun(b, rates));
}

TEST_CASE("econ catalog is internally consistent", "[economy]") {
    for (size_t i = 0; i < kEconCatalog.size(); ++i) {
        const EconNode node    = static_cast<EconNode>(i);
        const EconNodeDef& def = econDef(node);
        REQUIRE(def.maxRank >= 1);
        REQUIRE(def.baseCost > 0u);
        REQUIRE(def.costGrowthDen > 0u);
        // Prereqs must point at an unlock node (only unlocks gate the tree).
        if (def.hasPrereq) {
            REQUIRE(econDef(def.prereq).isUnlock);
        }
        // Unlock nodes are single-rank gates.
        if (def.isUnlock) {
            REQUIRE(def.maxRank == 1);
        }
    }
}

TEST_CASE("nextCost grows geometrically and caps out at maxed/blocked nodes", "[economy]") {
    EconomyState state{};
    state.currency = 1000000u; // never the limiting factor here

    // HitscanDamage: base 20, growth 3/2, maxRank 3.
    REQUIRE(nextCost(state, EconNode::HitscanDamage) == 20u);
    REQUIRE(purchase(state, EconNode::HitscanDamage));
    REQUIRE(nextCost(state, EconNode::HitscanDamage) == 30u);        // 20*3/2
    REQUIRE(purchase(state, EconNode::HitscanDamage));
    REQUIRE(nextCost(state, EconNode::HitscanDamage) == 45u);        // 30*3/2
    REQUIRE(purchase(state, EconNode::HitscanDamage));
    REQUIRE(nextCost(state, EconNode::HitscanDamage) == UINT32_MAX); // maxRank reached

    // A prereq-gated node returns UINT32_MAX until its unlock is owned.
    EconomyState fresh{};
    fresh.currency = 1000000u;
    REQUIRE(nextCost(fresh, EconNode::RocketDamage) == UINT32_MAX); // Rocket not unlocked
    REQUIRE(purchase(fresh, EconNode::UnlockRocket));
    REQUIRE(nextCost(fresh, EconNode::RocketDamage) != UINT32_MAX); // now buyable
}

TEST_CASE("canPurchase is false when broke, maxed, or prereq unmet", "[economy]") {
    // Broke.
    EconomyState broke{};
    broke.currency = 5u; // HitscanDamage base is 20
    REQUIRE_FALSE(canPurchase(broke, EconNode::HitscanDamage));

    // Prereq unmet (Plasma slot locked).
    EconomyState rich{};
    rich.currency = 1000000u;
    REQUIRE_FALSE(canPurchase(rich, EconNode::PlasmaFireRate));

    // Maxed: buy an unlock, then it can't be bought again.
    REQUIRE(canPurchase(rich, EconNode::UnlockRocket));
    REQUIRE(purchase(rich, EconNode::UnlockRocket));
    REQUIRE_FALSE(canPurchase(rich, EconNode::UnlockRocket));
}

TEST_CASE("purchase deducts the exact cost and bumps rank/unlock mask", "[economy]") {
    EconomyState state{};
    state.currency = 100u;

    // Unlock node flips the mask bit and consumes exactly the base cost (40).
    REQUIRE(purchase(state, EconNode::UnlockRocket));
    REQUIRE(state.currency == 60u);
    REQUIRE(isUnlocked(state, EconNode::UnlockRocket));
    REQUIRE(rankOf(state, EconNode::UnlockRocket) == 1u);

    // Ranked node bumps its rank and deducts its cost.
    const uint32_t before = state.currency;
    const uint32_t cost   = nextCost(state, EconNode::RocketDamage);
    REQUIRE(purchase(state, EconNode::RocketDamage));
    REQUIRE(state.currency == before - cost);
    REQUIRE(rankOf(state, EconNode::RocketDamage) == 1u);

    // Unaffordable purchase is a no-op returning false.
    EconomyState poor{};
    poor.currency               = 1u;
    const EconomyState snapshot = poor;
    REQUIRE_FALSE(purchase(poor, EconNode::HitscanDamage));
    REQUIRE(poor.currency == snapshot.currency);
    REQUIRE(rankOf(poor, EconNode::HitscanDamage) == 0u);
}

TEST_CASE("modsForWeapon matches an equivalent accumulate() upgrade list", "[economy]") {
    EconomyState state{};
    state.currency = 1000000u;

    // Two ranks of HitscanDamage (+25% each) and one of HitscanFireRate (+20%).
    REQUIRE(purchase(state, EconNode::HitscanDamage));
    REQUIRE(purchase(state, EconNode::HitscanDamage));
    REQUIRE(purchase(state, EconNode::HitscanFireRate));

    const WeaponMods mods = modsForWeapon(state, /*Hitscan*/ 0);

    // Equivalent flat upgrade list fed through the existing WeaponUpgrade math.
    const std::vector<WeaponUpgrade> upgrades = {
        {WeaponUpgradeKind::Damage, 1.25f},
        {WeaponUpgradeKind::Damage, 1.25f},
        {WeaponUpgradeKind::FireRate, 1.2f},
    };
    const WeaponMods expected = accumulate(upgrades);

    REQUIRE(mods.damageMul == Catch::Approx(expected.damageMul));
    REQUIRE(mods.fireRateMul == Catch::Approx(expected.fireRateMul));
    REQUIRE(mods.splashMul == Catch::Approx(expected.splashMul));
    REQUIRE(mods.projSpeedMul == Catch::Approx(expected.projSpeedMul));

    // A weapon slot with no owned upgrades is identity.
    const WeaponMods rocketMods = modsForWeapon(state, /*Rocket*/ 1);
    REQUIRE(rocketMods.damageMul == Catch::Approx(1.f));
    REQUIRE(rocketMods.splashMul == Catch::Approx(1.f));
}

TEST_CASE("readEconomy inverts writeEconomy", "[economy]") {
    EconomyState state{};
    state.currency = 1234u;
    // Exercise every node's rank word (ranks 0..7 -> both packed words).
    state.unlockMask =
        (1u << static_cast<uint32_t>(EconNode::UnlockRocket)) | (1u << static_cast<uint32_t>(EconNode::UnlockPlasma));
    state.ranks[static_cast<size_t>(EconNode::UnlockRocket)]    = 1;
    state.ranks[static_cast<size_t>(EconNode::UnlockPlasma)]    = 1;
    state.ranks[static_cast<size_t>(EconNode::HitscanDamage)]   = 3;
    state.ranks[static_cast<size_t>(EconNode::HitscanFireRate)] = 2;
    state.ranks[static_cast<size_t>(EconNode::RocketDamage)]    = 3;
    state.ranks[static_cast<size_t>(EconNode::RocketSplash)]    = 1;
    state.ranks[static_cast<size_t>(EconNode::PlasmaFireRate)]  = 2;
    state.ranks[static_cast<size_t>(EconNode::PlasmaProjSpeed)] = 2;

    SaveData save{};
    writeEconomy(save, state);
    const EconomyState back = readEconomy(save);

    REQUIRE(back.currency == state.currency);
    REQUIRE(back.unlockMask == state.unlockMask);
    REQUIRE(back.ranks == state.ranks);
}

TEST_CASE("economy survives a SaveData serialize/parse round-trip", "[economy]") {
    EconomyState state{};
    state.currency = 4242u;
    REQUIRE(purchase(state, EconNode::UnlockRocket));                  // needs currency; grant it first
    state.currency                                            = 4242u; // reset to a known value post-unlock
    state.ranks[static_cast<size_t>(EconNode::HitscanDamage)] = 2;

    SaveData save{};
    writeEconomy(save, state);

    const std::optional<SaveData> parsed = parseSave(serializeSave(save));
    REQUIRE(parsed.has_value());
    const EconomyState back = readEconomy(*parsed);
    REQUIRE(back.currency == state.currency);
    REQUIRE(back.unlockMask == state.unlockMask);
    REQUIRE(back.ranks == state.ranks);
}

// Regression: MetaProgression::Unlock bits and WeaponEconomy EconNode unlock
// bits live in SEPARATE SaveData fields (unlockFlags vs econUnlockMask), so
// neither subsystem can clobber or forge the other's unlocks. Both enums start
// at bit 0, so a single shared field would have aliased them.
TEST_CASE("meta unlocks and economy unlocks are independent (no bit aliasing)", "[economy][meta]") {
    SaveData save{};

    // Meta side: earn wave-threshold auto-unlocks (sets low bits of unlockFlags).
    setUnlock(save, Unlock::AltFire);     // bit 0 in unlockFlags
    setUnlock(save, Unlock::ExtraWeapon); // bit 1 in unlockFlags
    setUnlock(save, Unlock::HardMode);    // bit 2 in unlockFlags

    // Economy side: build an economy that has NOT purchased any weapon-slot
    // unlock, then persist it. If the two shared a field, this write would wipe
    // the meta bits (plain assignment) — here it must leave them untouched.
    EconomyState econ{};
    writeEconomy(save, econ);
    REQUIRE(isUnlocked(save, Unlock::AltFire));
    REQUIRE(isUnlocked(save, Unlock::ExtraWeapon));
    REQUIRE(isUnlocked(save, Unlock::HardMode));

    // Now purchase an economy unlock and persist. Meta bits still survive, and
    // the economy unlock does NOT read back as a phantom meta unlock.
    econ.currency = 1000u;
    REQUIRE(purchase(econ, EconNode::UnlockRocket)); // econ bit 0, but in econUnlockMask
    writeEconomy(save, econ);

    const std::optional<SaveData> parsed = parseSave(serializeSave(save));
    REQUIRE(parsed.has_value());
    // Meta unlocks intact after two economy writes + a round-trip.
    REQUIRE(isUnlocked(*parsed, Unlock::AltFire));
    REQUIRE(isUnlocked(*parsed, Unlock::HardMode));
    // Economy unlock present on its own field.
    REQUIRE(isUnlocked(readEconomy(*parsed), EconNode::UnlockRocket));
    // And crucially: the economy's purchased UnlockRocket did NOT forge a meta
    // Unlock at the same bit index, nor vice-versa (they are in disjoint words).
    REQUIRE_FALSE(isUnlocked(*parsed, Unlock::ArenaTheme));                  // bit 3, never set by either side
    REQUIRE_FALSE(isUnlocked(readEconomy(*parsed), EconNode::UnlockPlasma)); // econ bit 1 never purchased
}
