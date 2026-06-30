#include "engine/script/LuaVec3.h"

namespace ds::lua {

namespace {

// Single-letter field name -> pointer to that component, or nullptr.
float* component(glm::vec3& v, const char* key) {
    if (key[0] == '\0' || key[1] != '\0')
        return nullptr;
    switch (key[0]) {
    case 'x':
        return &v.x;
    case 'y':
        return &v.y;
    case 'z':
        return &v.z;
    default:
        return nullptr;
    }
}

int l_vec3_index(lua_State* L) {
    glm::vec3* v       = checkUserdata<glm::vec3>(L, 1);
    const char* key     = luaL_checkstring(L, 2);
    if (const float* c = component(*v, key)) {
        lua_pushnumber(L, static_cast<lua_Number>(*c));
        return 1;
    }
    return methodFallback(L, UserdataTraits<glm::vec3>::name);
}

int l_vec3_newindex(lua_State* L) {
    glm::vec3* v   = checkUserdata<glm::vec3>(L, 1);
    const char* key = luaL_checkstring(L, 2);
    float* c        = component(*v, key);
    if (!c)
        return luaL_error(L, "ds.Vec3 has no field '%s'", key);
    *c = static_cast<float>(luaL_checknumber(L, 3));
    return 0;
}

int l_vec3_add(lua_State* L) {
    glm::vec3* a = checkUserdata<glm::vec3>(L, 1);
    glm::vec3* b = checkUserdata<glm::vec3>(L, 2);
    pushUserdata<glm::vec3>(L, *a + *b);
    return 1;
}

int l_vec3_sub(lua_State* L) {
    glm::vec3* a = checkUserdata<glm::vec3>(L, 1);
    glm::vec3* b = checkUserdata<glm::vec3>(L, 2);
    pushUserdata<glm::vec3>(L, *a - *b);
    return 1;
}

// Vec3 * number only; either operand may be the Vec3 (Lua calls __mul(a, b)
// regardless of which side held the userdata). Vec3 * Vec3 is rejected.
int l_vec3_mul(lua_State* L) {
    glm::vec3* v = testUserdata<glm::vec3>(L, 1);
    int numIdx   = 2;
    if (!v) {
        v     = testUserdata<glm::vec3>(L, 2);
        numIdx = 1;
    }
    if (!v || !lua_isnumber(L, numIdx))
        return luaL_error(L, "ds.Vec3 __mul expects Vec3 * number");
    float scalar = static_cast<float>(lua_tonumber(L, numIdx));
    pushUserdata<glm::vec3>(L, *v * scalar);
    return 1;
}

int l_vec3_unm(lua_State* L) {
    glm::vec3* v = checkUserdata<glm::vec3>(L, 1);
    pushUserdata<glm::vec3>(L, -(*v));
    return 1;
}

int l_vec3_eq(lua_State* L) {
    glm::vec3* a = checkUserdata<glm::vec3>(L, 1);
    glm::vec3* b = checkUserdata<glm::vec3>(L, 2);
    lua_pushboolean(L, *a == *b ? 1 : 0);
    return 1;
}

int l_vec3_tostring(lua_State* L) {
    glm::vec3* v = checkUserdata<glm::vec3>(L, 1);
    lua_pushfstring(L, "Vec3(%f, %f, %f)", static_cast<double>(v->x), static_cast<double>(v->y),
                     static_cast<double>(v->z));
    return 1;
}

int l_vec3_length(lua_State* L) {
    glm::vec3* v = checkUserdata<glm::vec3>(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(glm::length(*v)));
    return 1;
}

int l_vec3_normalize(lua_State* L) {
    glm::vec3* v = checkUserdata<glm::vec3>(L, 1);
    glm::vec3 n  = glm::length(*v) > 0.f ? glm::normalize(*v) : glm::vec3{0.f};
    pushUserdata<glm::vec3>(L, n);
    return 1;
}

int l_vec3_dot(lua_State* L) {
    glm::vec3* a = checkUserdata<glm::vec3>(L, 1);
    glm::vec3* b = checkUserdata<glm::vec3>(L, 2);
    lua_pushnumber(L, static_cast<lua_Number>(glm::dot(*a, *b)));
    return 1;
}

int l_vec3_cross(lua_State* L) {
    glm::vec3* a = checkUserdata<glm::vec3>(L, 1);
    glm::vec3* b = checkUserdata<glm::vec3>(L, 2);
    pushUserdata<glm::vec3>(L, glm::cross(*a, *b));
    return 1;
}

const luaL_Reg kVec3Methods[] = {
    {"length", l_vec3_length}, {"normalize", l_vec3_normalize}, {"dot", l_vec3_dot},
    {"cross", l_vec3_cross},   {nullptr, nullptr},
};

int l_vec3_new(lua_State* L) {
    float x = static_cast<float>(luaL_optnumber(L, 1, 0.0));
    float y = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    float z = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    pushUserdata<glm::vec3>(L, glm::vec3{x, y, z});
    return 1;
}

} // namespace

void registerVec3(lua_State* L) {
    TypeSpec spec{};
    spec.name     = UserdataTraits<glm::vec3>::name;
    spec.methods  = kVec3Methods;
    spec.index    = l_vec3_index;
    spec.newindex = l_vec3_newindex;
    spec.gc       = gcUserdata<glm::vec3>;
    spec.tostring = l_vec3_tostring;
    spec.add      = l_vec3_add;
    spec.sub      = l_vec3_sub;
    spec.mul      = l_vec3_mul;
    spec.unm      = l_vec3_unm;
    spec.eq       = l_vec3_eq;
    registerType(L, spec);

    lua_newtable(L);
    lua_pushcfunction(L, l_vec3_new);
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "Vec3");
}

} // namespace ds::lua
