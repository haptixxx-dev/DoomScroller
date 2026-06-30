#include "engine/script/LuaGlobal.h"

#include "engine/ScriptSystem.h"
#include "engine/script/LuaUserdata.h"
#include "engine/script/LuaVec3.h"

#include <lua.hpp>

#include <cstring>

namespace ds::lua {

namespace {

// --- ds.Global.camera (read/write) -----------------------------------------

int l_camera_index(lua_State* L) {
    ScriptSystem* self    = ScriptSystem::fromState(L);
    const char* key        = luaL_checkstring(L, 2);
    const auto& cam        = self->callbacks().camera;
    if (std::strcmp(key, "position") == 0) {
        if (cam.getPosition)
            pushUserdata<glm::vec3>(L, cam.getPosition());
        else
            lua_pushnil(L);
        return 1;
    }
    if (std::strcmp(key, "yaw") == 0) {
        lua_pushnumber(L, cam.getYaw ? static_cast<lua_Number>(cam.getYaw()) : 0.0);
        return 1;
    }
    if (std::strcmp(key, "pitch") == 0) {
        lua_pushnumber(L, cam.getPitch ? static_cast<lua_Number>(cam.getPitch()) : 0.0);
        return 1;
    }
    if (std::strcmp(key, "fovY") == 0) {
        lua_pushnumber(L, cam.getFovY ? static_cast<lua_Number>(cam.getFovY()) : 0.0);
        return 1;
    }
    return luaL_error(L, "ds.Global.camera has no field '%s'", key);
}

int l_camera_newindex(lua_State* L) {
    ScriptSystem* self = ScriptSystem::fromState(L);
    const char* key     = luaL_checkstring(L, 2);
    const auto& cam     = self->callbacks().camera;
    if (std::strcmp(key, "position") == 0) {
        glm::vec3* v = checkUserdata<glm::vec3>(L, 3);
        if (cam.setPosition)
            cam.setPosition(*v);
        return 0;
    }
    if (std::strcmp(key, "yaw") == 0) {
        float v = static_cast<float>(luaL_checknumber(L, 3));
        if (cam.setYaw)
            cam.setYaw(v);
        return 0;
    }
    if (std::strcmp(key, "pitch") == 0) {
        float v = static_cast<float>(luaL_checknumber(L, 3));
        if (cam.setPitch)
            cam.setPitch(v);
        return 0;
    }
    if (std::strcmp(key, "fovY") == 0) {
        float v = static_cast<float>(luaL_checknumber(L, 3));
        if (cam.setFovY)
            cam.setFovY(v);
        return 0;
    }
    return luaL_error(L, "ds.Global.camera has no field '%s'", key);
}

// --- ds.Global.player (read-only except .health) ---------------------------

int l_player_index(lua_State* L) {
    ScriptSystem* self = ScriptSystem::fromState(L);
    const char* key     = luaL_checkstring(L, 2);
    const auto& pl      = self->callbacks().player;
    if (std::strcmp(key, "health") == 0) {
        lua_pushinteger(L, pl.getHealth ? pl.getHealth() : 0);
        return 1;
    }
    if (std::strcmp(key, "maxHealth") == 0) {
        lua_pushinteger(L, pl.getMaxHealth ? pl.getMaxHealth() : 0);
        return 1;
    }
    if (std::strcmp(key, "dashCharges") == 0) {
        lua_pushinteger(L, pl.getDashCharges ? pl.getDashCharges() : 0);
        return 1;
    }
    if (std::strcmp(key, "sliding") == 0) {
        lua_pushboolean(L, (pl.isSliding && pl.isSliding()) ? 1 : 0);
        return 1;
    }
    if (std::strcmp(key, "iFrames") == 0) {
        lua_pushnumber(L, pl.getIFrames ? static_cast<lua_Number>(pl.getIFrames()) : 0.0);
        return 1;
    }
    if (std::strcmp(key, "eyePosition") == 0) {
        if (pl.getEyePosition)
            pushUserdata<glm::vec3>(L, pl.getEyePosition());
        else
            lua_pushnil(L);
        return 1;
    }
    return luaL_error(L, "ds.Global.player has no field '%s'", key);
}

int l_player_newindex(lua_State* L) {
    ScriptSystem* self = ScriptSystem::fromState(L);
    const char* key     = luaL_checkstring(L, 2);
    const auto& pl      = self->callbacks().player;
    if (std::strcmp(key, "health") == 0) {
        int v = static_cast<int>(luaL_checkinteger(L, 3));
        if (pl.setHealth)
            pl.setHealth(v);
        return 0;
    }
    return luaL_error(L, "ds.Global.player.%s is read-only", key);
}

// --- ds.Global.time (read-only) ---------------------------------------------

int l_time_index(lua_State* L) {
    ScriptSystem* self = ScriptSystem::fromState(L);
    const char* key     = luaL_checkstring(L, 2);
    const auto& t       = self->callbacks().time;
    if (std::strcmp(key, "dt") == 0) {
        lua_pushnumber(L, t.getDt ? static_cast<lua_Number>(t.getDt()) : 0.0);
        return 1;
    }
    if (std::strcmp(key, "elapsed") == 0) {
        lua_pushnumber(L, t.getElapsed ? static_cast<lua_Number>(t.getElapsed()) : 0.0);
        return 1;
    }
    return luaL_error(L, "ds.Global.time has no field '%s'", key);
}

int l_time_newindex(lua_State* L) {
    return luaL_error(L, "ds.Global.time fields are read-only");
}

// Builds one organized sub-table (a plain table with an __index/__newindex
// dispatcher metatable) and leaves it on top of the stack.
void pushSubTable(lua_State* L, lua_CFunction index, lua_CFunction newindex) {
    lua_newtable(L);
    lua_newtable(L); // metatable
    lua_pushcfunction(L, index);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, newindex);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
}

} // namespace

void registerGlobalTable(lua_State* L) {
    lua_newtable(L); // ds.Global

    pushSubTable(L, l_camera_index, l_camera_newindex);
    lua_setfield(L, -2, "camera");

    pushSubTable(L, l_player_index, l_player_newindex);
    lua_setfield(L, -2, "player");

    pushSubTable(L, l_time_index, l_time_newindex);
    lua_setfield(L, -2, "time");

    // ds must already exist (registerBindings() runs first).
    lua_getglobal(L, "ds");
    lua_pushvalue(L, -2); // the ds.Global table
    lua_setfield(L, -2, "Global");
    lua_pop(L, 2); // ds, ds.Global (Global is now stored under ds)
}

} // namespace ds::lua
