#include "engine/WeaponUpgrade.h"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <span>

using namespace ds;

TEST_CASE("applyUpgrade stacks multiplicatively within a kind", "[upgrade]") {
    WeaponMods mods{};
    applyUpgrade(mods, WeaponUpgrade{WeaponUpgradeKind::Damage, 1.25f});
    REQUIRE(mods.damageMul == Catch::Approx(1.25f));
    applyUpgrade(mods, WeaponUpgrade{WeaponUpgradeKind::Damage, 1.25f});
    REQUIRE(mods.damageMul == Catch::Approx(1.5625f));
    // Other fields untouched.
    REQUIRE(mods.fireRateMul == Catch::Approx(1.f));
    REQUIRE(mods.splashMul == Catch::Approx(1.f));
    REQUIRE(mods.projSpeedMul == Catch::Approx(1.f));
}

TEST_CASE("applyUpgrade routes each kind to its own field", "[upgrade]") {
    WeaponMods mods{};
    applyUpgrade(mods, WeaponUpgrade{WeaponUpgradeKind::FireRate, 2.f});
    applyUpgrade(mods, WeaponUpgrade{WeaponUpgradeKind::Splash, 1.5f});
    applyUpgrade(mods, WeaponUpgrade{WeaponUpgradeKind::ProjectileSpeed, 3.f});
    REQUIRE(mods.damageMul == Catch::Approx(1.f));
    REQUIRE(mods.fireRateMul == Catch::Approx(2.f));
    REQUIRE(mods.splashMul == Catch::Approx(1.5f));
    REQUIRE(mods.projSpeedMul == Catch::Approx(3.f));
}

TEST_CASE("accumulate folds a span of upgrades", "[upgrade]") {
    const std::array<WeaponUpgrade, 4> upgrades{
        WeaponUpgrade{WeaponUpgradeKind::Damage, 1.25f},
        WeaponUpgrade{WeaponUpgradeKind::Damage, 1.25f},
        WeaponUpgrade{WeaponUpgradeKind::FireRate, 1.5f},
        WeaponUpgrade{WeaponUpgradeKind::ProjectileSpeed, 2.f},
    };
    WeaponMods mods = accumulate(std::span<const WeaponUpgrade>(upgrades));
    REQUIRE(mods.damageMul == Catch::Approx(1.5625f));
    REQUIRE(mods.fireRateMul == Catch::Approx(1.5f));
    REQUIRE(mods.splashMul == Catch::Approx(1.f));
    REQUIRE(mods.projSpeedMul == Catch::Approx(2.f));
}

TEST_CASE("accumulate of an empty span is identity", "[upgrade]") {
    WeaponMods mods = accumulate(std::span<const WeaponUpgrade>{});
    REQUIRE(mods.damageMul == Catch::Approx(1.f));
    REQUIRE(mods.fireRateMul == Catch::Approx(1.f));
    REQUIRE(mods.splashMul == Catch::Approx(1.f));
    REQUIRE(mods.projSpeedMul == Catch::Approx(1.f));
}

TEST_CASE("withMods scales the right fields and rounds damage", "[upgrade]") {
    WeaponComponent base{};
    base.damage          = 25;
    base.fireRate        = 5.f;
    base.splashRadius    = 2.f;
    base.projectileSpeed = 30.f;
    base.type            = WeaponType::Rocket;
    base.ammo            = 12;

    WeaponMods mods{};
    mods.damageMul    = 1.5625f; // 25 * 1.5625 = 39.0625 -> 39
    mods.fireRateMul  = 2.f;
    mods.splashMul    = 1.5f;
    mods.projSpeedMul = 3.f;

    WeaponComponent out = withMods(base, mods);
    REQUIRE(out.damage == 39);
    REQUIRE(out.fireRate == Catch::Approx(10.f));
    REQUIRE(out.splashRadius == Catch::Approx(3.f));
    REQUIRE(out.projectileSpeed == Catch::Approx(90.f));
    // type and ammo are left untouched.
    REQUIRE(out.type == WeaponType::Rocket);
    REQUIRE(out.ammo == 12);
}

TEST_CASE("withMods identity mods leave the weapon unchanged", "[upgrade]") {
    WeaponComponent base{};
    base.damage          = 25;
    base.fireRate        = 5.f;
    base.splashRadius    = 0.f;
    base.projectileSpeed = 30.f;

    WeaponComponent out = withMods(base, WeaponMods{});
    REQUIRE(out.damage == 25);
    REQUIRE(out.fireRate == Catch::Approx(5.f));
    REQUIRE(out.splashRadius == Catch::Approx(0.f));
    REQUIRE(out.projectileSpeed == Catch::Approx(30.f));
}

TEST_CASE("altFire data: shotgun fans out, others single", "[upgrade]") {
    REQUIRE(altFirePelletCount(AltFireMode::ShotgunSpread) > 1);
    REQUIRE(altFirePelletCount(AltFireMode::None) == 1);
    REQUIRE(altFirePelletCount(AltFireMode::ChargedRocket) == 1);
    REQUIRE(altFirePelletCount(AltFireMode::PlasmaOverheat) == 1);

    REQUIRE(altFireSpreadRadians(AltFireMode::ShotgunSpread) > 0.f);
    REQUIRE(altFireSpreadRadians(AltFireMode::None) == Catch::Approx(0.f));
}
