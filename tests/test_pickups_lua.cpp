#include "engine/ScriptSystem.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <string>

using namespace ds;

namespace {

// Loads the real, shipped assets/scripts/pickups.lua (DS_ASSETS_DIR is
// injected by tests/CMakeLists.txt), same pattern as test_parry_lua.cpp.
void loadPickupsScript(ScriptSystem& scripts) {
    REQUIRE(scripts.init());
    REQUIRE(scripts.loadFile(std::string(DS_ASSETS_DIR) + "/scripts/pickups.lua"));
}

} // namespace

// Ports tests/test_pickup.cpp's withinPickupRange/pickupEffectMagnitude cases
// onto ds.pickups.collect_check, driven via ScriptSystem::pickupCollectCheck.
TEST_CASE("pickupCollectCheck collects only inside the sphere", "[scripting][pickups]") {
    ScriptSystem scripts;
    loadPickupsScript(scripts);

    glm::vec3 player{0.f, 1.f, 0.f};
    REQUIRE(scripts.pickupCollectCheck(player, {0.f, 1.f, 0.f}, 1.5f, 25, 100).collected);       // coincident
    REQUIRE(scripts.pickupCollectCheck(player, {1.f, 1.f, 0.f}, 1.5f, 25, 100).collected);       // inside
    REQUIRE_FALSE(scripts.pickupCollectCheck(player, {3.f, 1.f, 0.f}, 1.5f, 25, 100).collected); // outside
}

TEST_CASE("pickupCollectCheck respects the full 3D distance", "[scripting][pickups]") {
    ScriptSystem scripts;
    loadPickupsScript(scripts);

    glm::vec3 player{0.f, 0.f, 0.f};
    REQUIRE_FALSE(scripts.pickupCollectCheck(player, {2.f, 2.f, 1.f}, 2.9f, 25, 100).collected);
    REQUIRE(scripts.pickupCollectCheck(player, {2.f, 2.f, 1.f}, 3.1f, 25, 100).collected);
}

TEST_CASE("pickupCollectCheck never collects with non-positive radius", "[scripting][pickups]") {
    ScriptSystem scripts;
    loadPickupsScript(scripts);

    glm::vec3 p{1.f, 1.f, 1.f};
    REQUIRE_FALSE(scripts.pickupCollectCheck(p, p, 0.f, 25, 100).collected);
    REQUIRE_FALSE(scripts.pickupCollectCheck(p, p, -1.f, 25, 100).collected);
}

TEST_CASE("pickupCollectCheck clamps the grant to available headroom", "[scripting][pickups]") {
    ScriptSystem scripts;
    loadPickupsScript(scripts);

    glm::vec3 p{0.f, 0.f, 0.f};
    REQUIRE(scripts.pickupCollectCheck(p, p, 1.f, 25, 100).grant == 25); // full value fits
    REQUIRE(scripts.pickupCollectCheck(p, p, 1.f, 25, 10).grant == 10);  // clamped to headroom
    REQUIRE(scripts.pickupCollectCheck(p, p, 1.f, 25, 0).grant == 0);    // no headroom -> nothing
}

TEST_CASE("pickupCollectCheck grant never goes negative", "[scripting][pickups]") {
    ScriptSystem scripts;
    loadPickupsScript(scripts);

    glm::vec3 p{0.f, 0.f, 0.f};
    REQUIRE(scripts.pickupCollectCheck(p, p, 1.f, -5, 100).grant == 0);
    REQUIRE(scripts.pickupCollectCheck(p, p, 1.f, 25, -5).grant == 0);
}

// No prior C++ test existed for the drop cadence (it lived inline in
// Engine::handleEnemyDeaths) — this is new coverage gained by the migration.
// Cadence is keyed on (kill_count / 3) % 3, so the cycle starts at index 1
// (kill_count==3 -> 3/3==1), not 0: drop order is Ammo, DashCharge, Health.
TEST_CASE("pickupRegisterKill drops every 3rd kill, cycling kind", "[scripting][pickups]") {
    ScriptSystem scripts;
    loadPickupsScript(scripts);

    PickupDrop d1 = scripts.pickupRegisterKill();
    REQUIRE_FALSE(d1.drop);
    PickupDrop d2 = scripts.pickupRegisterKill();
    REQUIRE_FALSE(d2.drop);

    PickupDrop d3 = scripts.pickupRegisterKill(); // 3rd kill: (3/3)%3=1 -> Ammo
    REQUIRE(d3.drop);
    REQUIRE(d3.kind == 1);
    REQUIRE(d3.value == 30);

    for (int i = 0; i < 5; ++i)
        scripts.pickupRegisterKill();

    PickupDrop d9 = scripts.pickupRegisterKill(); // 9th kill: (9/3)%3=0 -> Health
    REQUIRE(d9.drop);
    REQUIRE(d9.kind == 0);
    REQUIRE(d9.value == 25);

    for (int i = 0; i < 5; ++i)
        scripts.pickupRegisterKill();

    PickupDrop d15 = scripts.pickupRegisterKill(); // 15th kill: (15/3)%3=2 -> DashCharge
    REQUIRE(d15.drop);
    REQUIRE(d15.kind == 2);
    REQUIRE(d15.value == 1);
}

TEST_CASE("ds.pickups.reset clears the kill counter", "[scripting][pickups]") {
    ScriptSystem scripts;
    loadPickupsScript(scripts);

    scripts.pickupRegisterKill();
    scripts.pickupRegisterKill();
    PickupDrop before = scripts.pickupRegisterKill();
    REQUIRE(before.drop); // 3rd kill drops

    scripts.pickupsReset();
    PickupDrop afterReset1 = scripts.pickupRegisterKill();
    PickupDrop afterReset2 = scripts.pickupRegisterKill();
    REQUIRE_FALSE(afterReset1.drop);
    REQUIRE_FALSE(afterReset2.drop);
}

TEST_CASE("pickup wrappers are graceful when pickups.lua never loaded", "[scripting][pickups]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());
    // No loadFile() call: ds.pickups doesn't exist.
    scripts.pickupsReset();
    PickupDrop drop = scripts.pickupRegisterKill();
    REQUIRE_FALSE(drop.drop);
    PickupCollect cc = scripts.pickupCollectCheck({0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 5.f, 25, 100);
    REQUIRE_FALSE(cc.collected);
}
