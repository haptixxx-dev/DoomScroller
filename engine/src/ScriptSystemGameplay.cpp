// Typed ScriptSystem member wrappers for the gameplay systems migrated to Lua
// (parry today; wave/boss/pickups/enemy AI join in later steps). Split out of
// ScriptSystem.cpp so that file doesn't balloon as more systems land; stays
// free of EnTT/Jolt/Components.h so ds_script_tests keeps linking light.
#include "engine/ScriptSystem.h"

#include "engine/script/LuaUserdata.h"
#include "engine/script/LuaVec3.h"

#include <lua.hpp>

namespace ds {

void ScriptSystem::parryReset() {
    if (!m_state)
        return;
    callModuleFunction("parry", "reset", 0, 0);
}

void ScriptSystem::parryTrigger() {
    if (!m_state)
        return;
    callModuleFunction("parry", "trigger", 0, 0);
}

void ScriptSystem::parryTick(float dt) {
    if (!m_state)
        return;
    lua_pushnumber(m_state, static_cast<lua_Number>(dt));
    callModuleFunction("parry", "tick", 1, 0);
}

bool ScriptSystem::parryActive() const {
    if (!m_state)
        return false;
    callModuleFunction("parry", "active", 0, 1);
    bool active = lua_toboolean(m_state, -1) != 0;
    lua_pop(m_state, 1);
    return active;
}

glm::vec3 ScriptSystem::parryReflect(const glm::vec3& incoming, float speedBoost) const {
    glm::vec3 fallback = -incoming * speedBoost;
    if (!m_state)
        return fallback;
    lua::pushUserdata<glm::vec3>(m_state, incoming);
    lua_pushnumber(m_state, static_cast<lua_Number>(speedBoost));
    callModuleFunction("parry", "reflect_velocity", 2, 1);
    glm::vec3* result = lua::testUserdata<glm::vec3>(m_state, -1);
    glm::vec3 out      = result ? *result : fallback;
    lua_pop(m_state, 1);
    return out;
}

float ScriptSystem::parryDashRefund() const {
    constexpr float kFallback = 1.f;
    if (!m_state)
        return kFallback;
    float refund = kFallback;
    lua_getglobal(m_state, "ds");
    lua_getfield(m_state, -1, "parry"); // nil if parry.lua never loaded
    if (lua_istable(m_state, -1)) {
        lua_getfield(m_state, -1, "tuning");
        if (lua_istable(m_state, -1)) {
            lua_getfield(m_state, -1, "dash_refund");
            if (lua_isnumber(m_state, -1))
                refund = static_cast<float>(lua_tonumber(m_state, -1));
            lua_pop(m_state, 1); // dash_refund
        }
        lua_pop(m_state, 1); // tuning
    }
    lua_pop(m_state, 2); // parry, ds
    return refund;
}

} // namespace ds
