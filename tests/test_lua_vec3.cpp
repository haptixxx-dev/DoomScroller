#include "engine/ScriptSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ds;

// Exercises the ds.Vec3 userdata (engine/script/LuaVec3.*) through ScriptSystem
// only, the same way gameplay scripts will use it. Every case stashes its
// result into a plain global number so it can be read back via
// getGlobalNumber() without needing raw lua_State access from the test.
TEST_CASE("ds.Vec3 construction and field read/write", "[scripting][vec3]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());

    REQUIRE(scripts.doString(R"lua(
        local v = Vec3.new(1, 2, 3)
        vx, vy, vz = v.x, v.y, v.z
        v.x = 10
        vx_after = v.x
    )lua"));

    REQUIRE(scripts.getGlobalNumber("vx") == Catch::Approx(1.0));
    REQUIRE(scripts.getGlobalNumber("vy") == Catch::Approx(2.0));
    REQUIRE(scripts.getGlobalNumber("vz") == Catch::Approx(3.0));
    REQUIRE(scripts.getGlobalNumber("vx_after") == Catch::Approx(10.0));
}

TEST_CASE("ds.Vec3 defaults to the zero vector", "[scripting][vec3]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());

    REQUIRE(scripts.doString("local v = Vec3.new() zx, zy, zz = v.x, v.y, v.z"));
    REQUIRE(scripts.getGlobalNumber("zx") == Catch::Approx(0.0));
    REQUIRE(scripts.getGlobalNumber("zy") == Catch::Approx(0.0));
    REQUIRE(scripts.getGlobalNumber("zz") == Catch::Approx(0.0));
}

TEST_CASE("ds.Vec3 unknown field is nil on read, errors on write", "[scripting][vec3]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());

    // Reading an unrecognized key falls through to the method table (normal
    // Lua __index semantics) and is nil, not an error, since "w" isn't a method.
    REQUIRE(scripts.doString("local v = Vec3.new(1, 2, 3) w_is_nil = (v.w == nil) and 1 or 0"));
    REQUIRE(scripts.getGlobalNumber("w_is_nil") == 1.0);

    REQUIRE_FALSE(scripts.doString("local v = Vec3.new(1, 2, 3) v.w = 5"));
}

TEST_CASE("ds.Vec3 arithmetic metamethods", "[scripting][vec3]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());

    REQUIRE(scripts.doString(R"lua(
        local a = Vec3.new(1, 2, 3)
        local b = Vec3.new(4, 5, 6)
        local s = a + b
        sx, sy, sz = s.x, s.y, s.z
        local d = b - a
        dx, dy, dz = d.x, d.y, d.z
        local m = a * 2
        mx, my, mz = m.x, m.y, m.z
        local m2 = 2 * a
        m2x = m2.x
        local n = -a
        nx, ny, nz = n.x, n.y, n.z
        eq_same = (Vec3.new(1, 1, 1) == Vec3.new(1, 1, 1)) and 1 or 0
        eq_diff = (Vec3.new(1, 1, 1) == Vec3.new(1, 1, 2)) and 1 or 0
    )lua"));

    REQUIRE(scripts.getGlobalNumber("sx") == Catch::Approx(5.0));
    REQUIRE(scripts.getGlobalNumber("sy") == Catch::Approx(7.0));
    REQUIRE(scripts.getGlobalNumber("sz") == Catch::Approx(9.0));
    REQUIRE(scripts.getGlobalNumber("dx") == Catch::Approx(3.0));
    REQUIRE(scripts.getGlobalNumber("dy") == Catch::Approx(3.0));
    REQUIRE(scripts.getGlobalNumber("dz") == Catch::Approx(3.0));
    REQUIRE(scripts.getGlobalNumber("mx") == Catch::Approx(2.0));
    REQUIRE(scripts.getGlobalNumber("my") == Catch::Approx(4.0));
    REQUIRE(scripts.getGlobalNumber("mz") == Catch::Approx(6.0));
    REQUIRE(scripts.getGlobalNumber("m2x") == Catch::Approx(2.0));
    REQUIRE(scripts.getGlobalNumber("nx") == Catch::Approx(-1.0));
    REQUIRE(scripts.getGlobalNumber("ny") == Catch::Approx(-2.0));
    REQUIRE(scripts.getGlobalNumber("nz") == Catch::Approx(-3.0));
    REQUIRE(scripts.getGlobalNumber("eq_same") == 1.0);
    REQUIRE(scripts.getGlobalNumber("eq_diff") == 0.0);
}

TEST_CASE("ds.Vec3 rejects Vec3 * Vec3", "[scripting][vec3]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());
    REQUIRE_FALSE(scripts.doString("local _ = Vec3.new(1, 2, 3) * Vec3.new(1, 2, 3)"));
}

TEST_CASE("ds.Vec3 methods: length/normalize/dot/cross", "[scripting][vec3]") {
    ScriptSystem scripts;
    REQUIRE(scripts.init());

    REQUIRE(scripts.doString(R"lua(
        local a = Vec3.new(3, 4, 0)
        len = a:length()
        local n = a:normalize()
        nx, ny = n.x, n.y
        local b = Vec3.new(1, 0, 0)
        local c = Vec3.new(0, 1, 0)
        dotv = b:dot(c)
        local cr = b:cross(c)
        crx, cry, crz = cr.x, cr.y, cr.z
        str = tostring(Vec3.new(1, 2, 3))
    )lua"));

    REQUIRE(scripts.getGlobalNumber("len") == Catch::Approx(5.0));
    REQUIRE(scripts.getGlobalNumber("nx") == Catch::Approx(0.6));
    REQUIRE(scripts.getGlobalNumber("ny") == Catch::Approx(0.8));
    REQUIRE(scripts.getGlobalNumber("dotv") == Catch::Approx(0.0));
    REQUIRE(scripts.getGlobalNumber("crx") == Catch::Approx(0.0));
    REQUIRE(scripts.getGlobalNumber("cry") == Catch::Approx(0.0));
    REQUIRE(scripts.getGlobalNumber("crz") == Catch::Approx(1.0));
}
