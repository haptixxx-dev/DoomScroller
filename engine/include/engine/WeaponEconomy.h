#pragma once

#include "engine/SaveData.h"
#include "engine/WeaponUpgrade.h"

#include <array>
#include <cstdint>

// =============================================================================
// Weapon economy: run -> currency -> purchasable upgrade tree (Phase 4 task 52)
// =============================================================================
//
// A pure, data-only meta-economy layered on top of the existing multiplicative
// WeaponUpgrade math (WeaponUpgrade.h — NOT modified here). A finished run
// yields deterministic integer currency (currencyForRun); currency is spent on
// nodes of a small upgrade catalog (kEconCatalog), each an unlock or a ranked
// stat boost bound to one weapon slot. The accumulated ranks fold into a
// WeaponMods per weapon slot via modsForWeapon(), reusing WeaponUpgrade.h's
// applyUpgrade so the runtime firing systems consume it exactly as they consume
// intermission upgrades. EconomyState (de)serialises into SaveData v2 fields.
//
// Header-only + constexpr where possible; depends only on SaveData.h,
// WeaponUpgrade.h and the stdlib, so it links into the pure test target with no
// SDL3/Jolt/EnTT runtime state.
//
// WEAPON SLOTS mirror WeaponType (Components.h): 0 = Hitscan, 1 = Rocket,
// 2 = Plasma. Unlock nodes gate the Rocket/Plasma slots; the Hitscan slot is
// always available.
// =============================================================================

namespace ds {

// --- currency ----------------------------------------------------------------

// The run figures currency is derived from. Deterministic integer + one style
// float (style is floored, so the result stays deterministic).
struct RunEconomyStats {
    uint32_t kills     = 0;
    uint32_t score     = 0;
    uint32_t bestCombo = 0;
    float stylePoints  = 0.f;
};

// Per-figure currency weights. Defaults chosen so a typical run yields a few
// dozen currency. All integer except the style divisor.
struct CurrencyRates {
    uint32_t perKill      = 2u;  // currency per kill
    uint32_t scoreDivisor = 50u; // 1 currency per this many score points
    uint32_t perCombo     = 1u;  // currency per best-combo step
    uint32_t styleDivisor = 10u; // 1 currency per this many (floored) style points
};

// Deterministic integer currency earned by a run. Floors the style contribution
// so the whole computation is integer and reproducible.
inline uint32_t currencyForRun(const RunEconomyStats& stats, const CurrencyRates& rates = {}) {
    uint32_t total = 0;
    total += stats.kills * rates.perKill;
    if (rates.scoreDivisor != 0u) {
        total += stats.score / rates.scoreDivisor;
    }
    total += stats.bestCombo * rates.perCombo;
    const uint32_t style = static_cast<uint32_t>(stats.stylePoints > 0.f ? stats.stylePoints : 0.f);
    if (rates.styleDivisor != 0u) {
        total += style / rates.styleDivisor;
    }
    return total;
}

// --- catalog -----------------------------------------------------------------

// Nodes of the upgrade tree. Unlock nodes gate a weapon slot; the rest are
// ranked stat boosts bound to a slot. Append-only (values are packed into
// SaveData rank words by index, so reordering would remap saved ranks).
enum class EconNode : uint8_t {
    UnlockRocket,    // gate the Rocket slot
    UnlockPlasma,    // gate the Plasma slot
    HitscanDamage,   // +damage, Hitscan
    HitscanFireRate, // +fire rate, Hitscan
    RocketDamage,    // +damage, Rocket
    RocketSplash,    // +splash radius, Rocket
    PlasmaFireRate,  // +fire rate, Plasma
    PlasmaProjSpeed, // +projectile speed, Plasma
    Count,
};

// One catalog entry. weaponSlot mirrors WeaponType (0/1/2). isUnlock nodes have
// maxRank 1 and set the slot's unlock bit rather than a stat. Ranked nodes
// stack `multiplier` per rank on `kind`. Cost grows geometrically: rank r costs
// baseCost * (growthNum/growthDen)^r, integer-truncated each step. `prereq`
// (guarded by hasPrereq) must be unlocked/owned before this node is buyable.
struct EconNodeDef {
    int weaponSlot         = 0;
    bool isUnlock          = false;
    WeaponUpgradeKind kind = WeaponUpgradeKind::Damage;
    float multiplier       = 1.f;
    uint32_t baseCost      = 0;
    uint32_t costGrowthNum = 1;
    uint32_t costGrowthDen = 1;
    uint8_t maxRank        = 1;
    EconNode prereq        = EconNode::UnlockRocket;
    bool hasPrereq         = false;
};

// The upgrade catalog, indexed by EconNode. Order must match the enum.
inline constexpr std::array<EconNodeDef, static_cast<size_t>(EconNode::Count)> kEconCatalog{{
    // UnlockRocket: gate the Rocket slot.
    EconNodeDef{/*slot*/ 1, /*isUnlock*/ true, WeaponUpgradeKind::Damage, /*mult*/ 1.f, /*base*/ 40u,
                /*growthN*/ 1, /*growthD*/ 1, /*maxRank*/ 1, EconNode::UnlockRocket, /*hasPrereq*/ false},
    // UnlockPlasma: gate the Plasma slot.
    EconNodeDef{/*slot*/ 2, /*isUnlock*/ true, WeaponUpgradeKind::Damage, /*mult*/ 1.f, /*base*/ 80u,
                /*growthN*/ 1, /*growthD*/ 1, /*maxRank*/ 1, EconNode::UnlockRocket, /*hasPrereq*/ false},
    // HitscanDamage: +25% damage/rank, up to 3.
    EconNodeDef{/*slot*/ 0, /*isUnlock*/ false, WeaponUpgradeKind::Damage, /*mult*/ 1.25f, /*base*/ 20u,
                /*growthN*/ 3, /*growthD*/ 2, /*maxRank*/ 3, EconNode::UnlockRocket, /*hasPrereq*/ false},
    // HitscanFireRate: +20% fire rate/rank, up to 3.
    EconNodeDef{/*slot*/ 0, /*isUnlock*/ false, WeaponUpgradeKind::FireRate, /*mult*/ 1.2f, /*base*/ 25u,
                /*growthN*/ 3, /*growthD*/ 2, /*maxRank*/ 3, EconNode::UnlockRocket, /*hasPrereq*/ false},
    // RocketDamage: +30% damage/rank, up to 3. Needs the Rocket slot unlocked.
    EconNodeDef{/*slot*/ 1, /*isUnlock*/ false, WeaponUpgradeKind::Damage, /*mult*/ 1.3f, /*base*/ 30u,
                /*growthN*/ 3, /*growthD*/ 2, /*maxRank*/ 3, EconNode::UnlockRocket, /*hasPrereq*/ true},
    // RocketSplash: +25% splash/rank, up to 2. Needs the Rocket slot unlocked.
    EconNodeDef{/*slot*/ 1, /*isUnlock*/ false, WeaponUpgradeKind::Splash, /*mult*/ 1.25f, /*base*/ 35u,
                /*growthN*/ 3, /*growthD*/ 2, /*maxRank*/ 2, EconNode::UnlockRocket, /*hasPrereq*/ true},
    // PlasmaFireRate: +20% fire rate/rank, up to 3. Needs the Plasma slot unlocked.
    EconNodeDef{/*slot*/ 2, /*isUnlock*/ false, WeaponUpgradeKind::FireRate, /*mult*/ 1.2f, /*base*/ 30u,
                /*growthN*/ 3, /*growthD*/ 2, /*maxRank*/ 3, EconNode::UnlockPlasma, /*hasPrereq*/ true},
    // PlasmaProjSpeed: +15% projectile speed/rank, up to 2. Needs the Plasma slot unlocked.
    EconNodeDef{/*slot*/ 2, /*isUnlock*/ false, WeaponUpgradeKind::ProjectileSpeed, /*mult*/ 1.15f, /*base*/ 30u,
                /*growthN*/ 3, /*growthD*/ 2, /*maxRank*/ 2, EconNode::UnlockPlasma, /*hasPrereq*/ true},
}};

// The catalog entry for a node.
inline const EconNodeDef& econDef(EconNode node) {
    return kEconCatalog[static_cast<size_t>(node)];
}

// --- owned state --------------------------------------------------------------

// The player's owned economy: spendable currency, the unlock bitset (shared
// with SaveData::unlockFlags), and per-node ranks.
struct EconomyState {
    uint32_t currency   = 0;
    uint32_t unlockMask = 0;
    std::array<uint8_t, static_cast<size_t>(EconNode::Count)> ranks{};
};

// True if the given unlock node's bit is set in the mask.
inline bool isUnlocked(const EconomyState& state, EconNode node) {
    return (state.unlockMask & (1u << static_cast<uint32_t>(node))) != 0u;
}

// Current rank of a node (unlock nodes read as 1 once owned, else 0).
inline uint8_t rankOf(const EconomyState& state, EconNode node) {
    return state.ranks[static_cast<size_t>(node)];
}

// True if the node's prerequisite (if any) is satisfied. An unlock prereq is
// met when its bit is set; a non-unlock prereq is met when its rank > 0.
inline bool prereqMet(const EconomyState& state, EconNode node) {
    const EconNodeDef& def = econDef(node);
    if (!def.hasPrereq) {
        return true;
    }
    const EconNodeDef& pre = econDef(def.prereq);
    if (pre.isUnlock) {
        return isUnlocked(state, def.prereq);
    }
    return rankOf(state, def.prereq) > 0u;
}

// Cost of the NEXT rank of a node, or UINT32_MAX if the node is maxed out or
// its prerequisite is unmet. Rank r costs baseCost * (num/den)^r, truncated at
// each multiply step so the result is deterministic integer math.
inline uint32_t nextCost(const EconomyState& state, EconNode node) {
    const EconNodeDef& def = econDef(node);
    const uint8_t rank     = rankOf(state, node);
    if (rank >= def.maxRank) {
        return UINT32_MAX;
    }
    if (!prereqMet(state, node)) {
        return UINT32_MAX;
    }
    uint32_t cost = def.baseCost;
    for (uint8_t r = 0; r < rank; ++r) {
        cost = (cost * def.costGrowthNum) / def.costGrowthDen;
    }
    return cost;
}

// True if the node's next rank is affordable and legal (not maxed, prereq met).
inline bool canPurchase(const EconomyState& state, EconNode node) {
    const uint32_t cost = nextCost(state, node);
    return cost != UINT32_MAX && state.currency >= cost;
}

// Purchase the next rank of a node: deduct the exact cost, bump the rank (and,
// for unlock nodes, set the unlock bit). No-op returning false if unaffordable/
// illegal.
inline bool purchase(EconomyState& state, EconNode node) {
    if (!canPurchase(state, node)) {
        return false;
    }
    const uint32_t cost = nextCost(state, node);
    state.currency -= cost;
    ++state.ranks[static_cast<size_t>(node)];
    if (econDef(node).isUnlock) {
        state.unlockMask |= (1u << static_cast<uint32_t>(node));
    }
    return true;
}

// --- weapon mods --------------------------------------------------------------

// Fold the owned ranks that touch `weaponSlot` into a WeaponMods, starting from
// identity. Each ranked node applies its multiplier once per owned rank via the
// existing WeaponUpgrade.h applyUpgrade (so the result matches an equivalent
// accumulate() over a flat upgrade list). Unlock nodes carry no stat and are
// skipped.
inline WeaponMods modsForWeapon(const EconomyState& state, int weaponSlot) {
    WeaponMods mods{};
    for (size_t i = 0; i < kEconCatalog.size(); ++i) {
        const EconNodeDef& def = kEconCatalog[i];
        if (def.isUnlock || def.weaponSlot != weaponSlot) {
            continue;
        }
        const uint8_t rank = state.ranks[i];
        for (uint8_t r = 0; r < rank; ++r) {
            applyUpgrade(mods, WeaponUpgrade{def.kind, def.multiplier});
        }
    }
    return mods;
}

// --- SaveData packing ---------------------------------------------------------

// Pack a rank word: four uint8 ranks at [base .. base+3] little-endian.
namespace detail {
inline uint32_t packRankWord(const EconomyState& state, size_t base) {
    uint32_t word = 0;
    for (size_t k = 0; k < 4; ++k) {
        const size_t idx = base + k;
        if (idx < state.ranks.size()) {
            word |= static_cast<uint32_t>(state.ranks[idx]) << (k * 8u);
        }
    }
    return word;
}

inline void unpackRankWord(uint32_t word, std::array<uint8_t, static_cast<size_t>(EconNode::Count)>& ranks,
                           size_t base) {
    for (size_t k = 0; k < 4; ++k) {
        const size_t idx = base + k;
        if (idx < ranks.size()) {
            ranks[idx] = static_cast<uint8_t>((word >> (k * 8u)) & 0xFFu);
        }
    }
}
} // namespace detail

// Write the economy into a SaveData: currency -> currency, unlockMask ->
// unlockFlags (shared bitset), ranks -> the two packed rank words.
inline void writeEconomy(SaveData& save, const EconomyState& state) {
    save.currency      = state.currency;
    save.unlockFlags   = state.unlockMask;
    save.upgradeRanks0 = detail::packRankWord(state, 0);
    save.upgradeRanks1 = detail::packRankWord(state, 4);
}

// Read the economy back out of a SaveData (inverse of writeEconomy).
inline EconomyState readEconomy(const SaveData& save) {
    EconomyState state{};
    state.currency   = save.currency;
    state.unlockMask = save.unlockFlags;
    detail::unpackRankWord(save.upgradeRanks0, state.ranks, 0);
    detail::unpackRankWord(save.upgradeRanks1, state.ranks, 4);
    return state;
}

} // namespace ds
