#include "engine/ParryTech.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ds;

TEST_CASE("triggerParry opens window only when off cooldown", "[parry]") {
    ParryTuning tuning;
    ParryState state;

    triggerParry(state, tuning);
    REQUIRE(state.window == Catch::Approx(tuning.windowDuration));
    REQUIRE(state.cooldown == Catch::Approx(tuning.cooldown));
    REQUIRE(state.active());
}

TEST_CASE("a second trigger during cooldown is ignored", "[parry]") {
    ParryTuning tuning;
    ParryState state;

    triggerParry(state, tuning);
    // Let the active window close but keep cooldown running.
    tickParry(state, tuning.windowDuration);
    REQUIRE_FALSE(state.active());
    REQUIRE(state.cooldown > 0.f);

    float cooldownBefore = state.cooldown;
    triggerParry(state, tuning); // still cooling down -> no-op
    REQUIRE_FALSE(state.active());
    REQUIRE(state.window == Catch::Approx(0.f));
    REQUIRE(state.cooldown == Catch::Approx(cooldownBefore));
}

TEST_CASE("tickParry closes the window and clears the cooldown", "[parry]") {
    ParryTuning tuning;
    ParryState state;

    triggerParry(state, tuning);

    // After windowDuration the window is closed but cooldown still ticking.
    tickParry(state, tuning.windowDuration);
    REQUIRE(state.window == Catch::Approx(0.f));
    REQUIRE_FALSE(state.active());
    REQUIRE(state.cooldown > 0.f);

    // After the full cooldown both timers are clamped at zero.
    tickParry(state, tuning.cooldown);
    REQUIRE(state.cooldown == Catch::Approx(0.f));

    // Now a fresh parry is allowed again.
    triggerParry(state, tuning);
    REQUIRE(state.active());
}

TEST_CASE("parrySucceeds is true during the window, false after", "[parry]") {
    ParryTuning tuning;
    ParryState state;

    triggerParry(state, tuning);
    REQUIRE(parrySucceeds(state));

    tickParry(state, tuning.windowDuration * 0.5f);
    REQUIRE(parrySucceeds(state));           // half-way through, still open

    tickParry(state, tuning.windowDuration); // overshoot to close it
    REQUIRE_FALSE(parrySucceeds(state));
}

TEST_CASE("reflectProjectileVelocity flips sign and boosts magnitude", "[parry]") {
    glm::vec3 incoming{0.f, 0.f, 30.f};

    glm::vec3 reflected = reflectProjectileVelocity(incoming);
    REQUIRE(reflected.x == Catch::Approx(0.f));
    REQUIRE(reflected.y == Catch::Approx(0.f));
    REQUIRE(reflected.z == Catch::Approx(-45.f)); // -30 * 1.5

    // Default boost grows the speed.
    REQUIRE(glm::length(reflected) > glm::length(incoming));

    // Explicit boost of 1.0 is a pure flip (same magnitude).
    glm::vec3 flipped = reflectProjectileVelocity(incoming, 1.f);
    REQUIRE(flipped.z == Catch::Approx(-30.f));
    REQUIRE(glm::length(flipped) == Catch::Approx(glm::length(incoming)));
}

TEST_CASE("canDashCancel is true only while recovering", "[parry]") {
    REQUIRE(canDashCancel(0.25f));
    REQUIRE_FALSE(canDashCancel(0.f));
    REQUIRE_FALSE(canDashCancel(-0.1f));
}
