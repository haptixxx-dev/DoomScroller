#pragma once

#include "engine/script/LuaUserdata.h"

#include <glm/glm.hpp>

namespace ds::lua {

template <>
struct UserdataTraits<glm::vec3> {
    static constexpr const char* name = "ds.Vec3";
};

// Registers the ds.Vec3 metatable (fields x/y/z, +/-/unary-/scalar-* /==,
// :length()/:normalize()/:dot()/:cross(), tostring) and the global
// Vec3.new(x, y, z) constructor.
void registerVec3(lua_State* L);

} // namespace ds::lua
