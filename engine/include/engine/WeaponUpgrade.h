#pragma once

#include "engine/ecs/Components.h"

#include <cstdint>
#include <span>

namespace ds {

// Stackable, multiplicative weapon upgrades granted at intermission. Each
// upgrade scales one weapon stat by a multiplier; upgrades of the same kind
// compound (two +25% Damage upgrades => x1.5625). The math here is pure and
// data-only; the firing systems consume the accumulated WeaponMods via
// withMods() — see WeaponUpgrade integration notes for the wiring.
enum class WeaponUpgradeKind : uint8_t { Damage, FireRate, Splash, ProjectileSpeed };

// A single upgrade pickup: which stat it touches and by how much it scales.
// e.g. {WeaponUpgradeKind::Damage, 1.25f} is a +25% damage boost.
struct WeaponUpgrade {
    WeaponUpgradeKind kind = WeaponUpgradeKind::Damage;
    float multiplier       = 1.f;
};

// Accumulated multipliers for one weapon. Identity (all 1) leaves the base
// WeaponComponent unchanged. Applying upgrades folds their multipliers in.
struct WeaponMods {
    float damageMul    = 1.f;
    float fireRateMul  = 1.f;
    float splashMul    = 1.f;
    float projSpeedMul = 1.f;
};

// Fold a single upgrade into the matching field, stacking multiplicatively.
inline void applyUpgrade(WeaponMods& mods, const WeaponUpgrade& upgrade) {
    switch (upgrade.kind) {
    case WeaponUpgradeKind::Damage:
        mods.damageMul *= upgrade.multiplier;
        break;
    case WeaponUpgradeKind::FireRate:
        mods.fireRateMul *= upgrade.multiplier;
        break;
    case WeaponUpgradeKind::Splash:
        mods.splashMul *= upgrade.multiplier;
        break;
    case WeaponUpgradeKind::ProjectileSpeed:
        mods.projSpeedMul *= upgrade.multiplier;
        break;
    }
}

// Fold an entire list of upgrades into a single WeaponMods, starting from the
// identity. Order is irrelevant since each field accumulates by multiplication.
inline WeaponMods accumulate(std::span<const WeaponUpgrade> upgrades) {
    WeaponMods mods{};
    for (const WeaponUpgrade& upgrade : upgrades)
        applyUpgrade(mods, upgrade);
    return mods;
}

// Return a copy of base with damage / fireRate / splashRadius / projectileSpeed
// scaled by the accumulated mods. Damage is integer, so the scaled value is
// truncated with static_cast<int>. Type, ammo, cooldown, lifetime, muzzle
// offset and transient state are left untouched.
inline WeaponComponent withMods(const WeaponComponent& base, const WeaponMods& mods) {
    WeaponComponent out = base;
    out.damage          = static_cast<int>(static_cast<float>(base.damage) * mods.damageMul);
    out.fireRate        = base.fireRate * mods.fireRateMul;
    out.splashRadius    = base.splashRadius * mods.splashMul;
    out.projectileSpeed = base.projectileSpeed * mods.projSpeedMul;
    return out;
}

// Right-mouse alt-fire variants. The data/math (pellet count, spread cone) lives
// here; translating a held weapon + AltFireMode into actual hitscan rays /
// projectile spawns is the firing system's job.
enum class AltFireMode : uint8_t { None, ShotgunSpread, ChargedRocket, PlasmaOverheat };

// Number of pellets/rays a single alt-fire shot emits. ShotgunSpread fans out
// multiple pellets; the other modes fire a single (possibly buffed) shot.
inline int altFirePelletCount(AltFireMode mode) {
    switch (mode) {
    case AltFireMode::ShotgunSpread:
        return 8;
    case AltFireMode::None:
    case AltFireMode::ChargedRocket:
    case AltFireMode::PlasmaOverheat:
        return 1;
    }
    return 1;
}

// Half-angle (radians) of the cone pellets are scattered within. Only the
// spread variant disperses; the rest fire straight down the look direction.
inline float altFireSpreadRadians(AltFireMode mode) {
    switch (mode) {
    case AltFireMode::ShotgunSpread:
        return 0.12f;
    case AltFireMode::None:
    case AltFireMode::ChargedRocket:
    case AltFireMode::PlasmaOverheat:
        return 0.f;
    }
    return 0.f;
}

} // namespace ds
