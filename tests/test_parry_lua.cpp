#include "engine/ScriptSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <string>

using namespace ds;

namespace {

// Loads the real, shipped assets/scripts/parry.lua (DS_ASSETS_DIR is injected
// by tests/CMakeLists.txt) so these tests exercise exactly what the game runs,
// not a duplicated inline copy.
void loadParryScript(ScriptSystem& scripts) {
    REQUIRE(scripts.init());
    REQUIRE(scripts.loadFile(std::string(DS_ASSETS_DIR) + "/scripts/parry.lua"));
}

} // namespace

// Ports tests/test_parry.cpp's ParryTech.h cases onto the Lua port, driven
// through ScriptSystem::parry*() exactly as Engine.cpp now does.
TEST_CASE("ds.parry.trigger opens window only when off cooldown", "[scripting][parry]") {
    ScriptSystem scripts;
    loadParryScript(scripts);

    scripts.parryTrigger();
    REQUIRE(scripts.parryActive());
}

TEST_CASE("a second trigger during cooldown is ignored", "[scripting][parry]") {
    ScriptSystem scripts;
    loadParryScript(scripts);

    scripts.parryTrigger();
    // Let the active window close but keep cooldown running (window=0.3, cooldown=0.6).
    scripts.parryTick(0.3f);
    REQUIRE_FALSE(scripts.parryActive());

    scripts.parryTrigger(); // still cooling down -> no-op
    REQUIRE_FALSE(scripts.parryActive());
}

TEST_CASE("ds.parry.tick closes the window and clears the cooldown", "[scripting][parry]") {
    ScriptSystem scripts;
    loadParryScript(scripts);

    scripts.parryTrigger();

    // After windowDuration (0.3) the window is closed but cooldown still ticking.
    scripts.parryTick(0.3f);
    REQUIRE_FALSE(scripts.parryActive());

    // After the full cooldown (0.6) both timers are clamped at zero.
    scripts.parryTick(0.6f);

    // Now a fresh parry is allowed again.
    scripts.parryTrigger();
    REQUIRE(scripts.parryActive());
}

TEST_CASE("ds.parry.active is true during the window, false after", "[scripting][parry]") {
    ScriptSystem scripts;
    loadParryScript(scripts);

    scripts.parryTrigger();
    REQUIRE(scripts.parryActive());

    scripts.parryTick(0.15f); // half-way through the 0.3s window
    REQUIRE(scripts.parryActive());

    scripts.parryTick(0.3f); // overshoot to close it
    REQUIRE_FALSE(scripts.parryActive());
}

TEST_CASE("ds.parry.reflect_velocity flips sign and boosts magnitude", "[scripting][parry]") {
    ScriptSystem scripts;
    loadParryScript(scripts);

    glm::vec3 incoming{0.f, 0.f, 30.f};

    glm::vec3 reflected = scripts.parryReflect(incoming);
    REQUIRE(reflected.x == Catch::Approx(0.f));
    REQUIRE(reflected.y == Catch::Approx(0.f));
    REQUIRE(reflected.z == Catch::Approx(-45.f)); // -30 * 1.5
    REQUIRE(glm::length(reflected) > glm::length(incoming));

    // Explicit boost of 1.0 is a pure flip (same magnitude).
    glm::vec3 flipped = scripts.parryReflect(incoming, 1.f);
    REQUIRE(flipped.z == Catch::Approx(-30.f));
    REQUIRE(glm::length(flipped) == Catch::Approx(glm::length(incoming)));
}

TEST_CASE("ds.parry.tuning.dash_refund is readable via parryDashRefund", "[scripting][parry]") {
    ScriptSystem scripts;
    loadParryScript(scripts);
    REQUIRE(scripts.parryDashRefund() == Catch::Approx(1.f));
}

TEST_CASE("ds.parry.reset clears both timers", "[scripting][parry]") {
    ScriptSystem scripts;
    loadParryScript(scripts);

    scripts.parryTrigger();
    REQUIRE(scripts.parryActive());

    scripts.parryReset();
    REQUIRE_FALSE(scripts.parryActive());
    // Cooldown also cleared, so a trigger right after reset succeeds.
    scripts.parryTrigger();
    REQUIRE(scripts.parryActive());
}

TEST_CASE("parry wrappers are graceful when parry.lua never loaded", "[scripting][parry]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());
    // No loadFile() call: ds.parry doesn't exist.
    scripts.parryReset();
    scripts.parryTrigger();
    scripts.parryTick(0.016f);
    REQUIRE_FALSE(scripts.parryActive());
    REQUIRE(scripts.parryDashRefund() == Catch::Approx(1.f));
    glm::vec3 reflected = scripts.parryReflect({0.f, 0.f, 10.f});
    REQUIRE(reflected.z == Catch::Approx(-15.f)); // C++ fallback: -10 * 1.5
}
