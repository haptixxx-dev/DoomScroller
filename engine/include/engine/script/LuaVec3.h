#pragma once

#include "engine/script/LuaUserdata.h"

#include <glm/glm.hpp>

namespace ds::lua {

template<> struct UserdataTraits<glm::vec3> {
    static constexpr const char* name = "ds.Vec3";
};

/** Registers the ds.Vec3 userdata type and the global `Vec3` constructor table.
 *
 *  Lua-side API:
 *  - `Vec3.new(x, y, z)` -> a new ds.Vec3
 *  - fields `.x`, `.y`, `.z` (read/write numbers)
 *  - operators: `+`, `-` (binary and unary negation), `*` by a scalar, `==`
 *  - methods: `:length()`, `:normalize()`, `:dot(other)`, `:cross(other)`
 *  - `tostring(v)` renders as `"Vec3(x, y, z)"`
 *
 *  Every gameplay position/velocity/color passed between C++ and Lua (level
 *  geometry, projectiles, parry reflection, boss attack patterns, ...) is a
 *  ds.Vec3, backed 1:1 by glm::vec3 on the C++ side.
 */
void registerVec3(lua_State* L);

} // namespace ds::lua
