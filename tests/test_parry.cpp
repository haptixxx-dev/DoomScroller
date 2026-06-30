#include "engine/ParryTech.h"

#include <catch2/catch_test_macros.hpp>

using namespace ds;

// The rest of ParryTech.h's logic (ParryState/ParryTuning, triggerParry/
// tickParry/parrySucceeds/reflectProjectileVelocity) moved to Lua
// (assets/scripts/parry.lua, see tests/test_parry_lua.cpp). canDashCancel is
// unrelated dash-recovery math, never called from Engine.cpp, and stays here.
TEST_CASE("canDashCancel is true only while recovering", "[parry]") {
    REQUIRE(canDashCancel(0.25f));
    REQUIRE_FALSE(canDashCancel(0.f));
    REQUIRE_FALSE(canDashCancel(-0.1f));
}
