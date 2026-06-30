// Typed ScriptSystem member wrappers for the gameplay systems migrated to Lua
// (parry today; wave/boss/pickups/enemy AI join in later steps). Split out of
// ScriptSystem.cpp so that file doesn't balloon as more systems land; stays
// free of EnTT/Jolt/Components.h so ds_script_tests keeps linking light.
#include "engine/ScriptSystem.h"

#include "engine/script/LuaUserdata.h"
#include "engine/script/LuaVec3.h"

#include <lua.hpp>

namespace ds {

namespace {

int tableIntField(lua_State* L, int idx, const char* key, int fallback) {
    lua_getfield(L, idx, key);
    int out = lua_isnumber(L, -1) ? static_cast<int>(lua_tointeger(L, -1)) : fallback;
    lua_pop(L, 1);
    return out;
}

float tableFloatField(lua_State* L, int idx, const char* key, float fallback) {
    lua_getfield(L, idx, key);
    float out = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : fallback;
    lua_pop(L, 1);
    return out;
}

bool tableBoolField(lua_State* L, int idx, const char* key, bool fallback) {
    lua_getfield(L, idx, key);
    bool out = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : fallback;
    lua_pop(L, 1);
    return out;
}

} // namespace

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

PickupDrop ScriptSystem::pickupRegisterKill() {
    PickupDrop out{};
    if (!m_state)
        return out;
    callModuleFunction("pickups", "register_kill", 0, 3);
    out.drop  = lua_toboolean(m_state, -3) != 0;
    out.kind  = static_cast<int>(lua_tointeger(m_state, -2));
    out.value = static_cast<int>(lua_tointeger(m_state, -1));
    lua_pop(m_state, 3);
    return out;
}

PickupCollect ScriptSystem::pickupCollectCheck(const glm::vec3& playerPos, const glm::vec3& pickupPos, float radius,
                                                int value, int headroom) const {
    PickupCollect out{};
    if (!m_state)
        return out;
    lua::pushUserdata<glm::vec3>(m_state, playerPos);
    lua::pushUserdata<glm::vec3>(m_state, pickupPos);
    lua_pushnumber(m_state, static_cast<lua_Number>(radius));
    lua_pushinteger(m_state, value);
    lua_pushinteger(m_state, headroom);
    callModuleFunction("pickups", "collect_check", 5, 2);
    out.collected = lua_toboolean(m_state, -2) != 0;
    out.grant     = static_cast<int>(lua_tointeger(m_state, -1));
    lua_pop(m_state, 2);
    return out;
}

void ScriptSystem::pickupsReset() {
    if (!m_state)
        return;
    callModuleFunction("pickups", "reset", 0, 0);
}

WaveState ScriptSystem::readWaveState() const {
    WaveState out{};
    if (!m_state)
        return out;
    lua_getglobal(m_state, "ds");
    lua_getfield(m_state, -1, "wave");
    if (lua_istable(m_state, -1)) {
        lua_getfield(m_state, -1, "state");
        if (lua_istable(m_state, -1)) {
            int idx              = lua_gettop(m_state);
            out.wave              = tableIntField(m_state, idx, "wave", out.wave);
            out.aliveEnemies      = tableIntField(m_state, idx, "alive_enemies", out.aliveEnemies);
            out.intermission      = tableFloatField(m_state, idx, "intermission", out.intermission);
            out.intermissionArmed = tableBoolField(m_state, idx, "intermission_armed", out.intermissionArmed);
            out.spawnPending      = tableBoolField(m_state, idx, "spawn_pending", out.spawnPending);
            out.cleared           = tableBoolField(m_state, idx, "cleared", out.cleared);
            out.kills             = tableIntField(m_state, idx, "kills", out.kills);
            out.score             = tableIntField(m_state, idx, "score", out.score);
            out.timeSurvived      = tableFloatField(m_state, idx, "time_survived", out.timeSurvived);
            out.combo             = tableIntField(m_state, idx, "combo", out.combo);
            out.bestCombo         = tableIntField(m_state, idx, "best_combo", out.bestCombo);
            out.comboTimer        = tableFloatField(m_state, idx, "combo_timer", out.comboTimer);
        }
        lua_pop(m_state, 1); // state
    }
    lua_pop(m_state, 2); // wave, ds
    return out;
}

void ScriptSystem::waveReset() {
    if (!m_state)
        return;
    callModuleFunction("wave", "reset", 0, 0);
}

void ScriptSystem::waveTick(float dt) {
    if (!m_state)
        return;
    lua_pushnumber(m_state, static_cast<lua_Number>(dt));
    callModuleFunction("wave", "tick", 1, 0);
}

void ScriptSystem::waveRegisterKill() {
    if (!m_state)
        return;
    callModuleFunction("wave", "register_kill", 0, 0);
}

void ScriptSystem::waveAdvance() {
    if (!m_state)
        return;
    callModuleFunction("wave", "advance", 0, 0);
}

int ScriptSystem::waveEnemiesForWave(int wave) const {
    if (!m_state)
        return 0;
    lua_pushinteger(m_state, wave);
    callModuleFunction("wave", "enemies_for_wave", 1, 1);
    int n = static_cast<int>(lua_tointeger(m_state, -1));
    lua_pop(m_state, 1);
    return n;
}

void ScriptSystem::waveSetAliveEnemies(int n) {
    if (!m_state)
        return;
    lua_pushinteger(m_state, n);
    callModuleFunction("wave", "set_alive", 1, 0);
}

void ScriptSystem::waveArmIntermission() {
    if (!m_state)
        return;
    callModuleFunction("wave", "arm_intermission", 0, 0);
}

void ScriptSystem::waveMarkSpawned(int aliveCount) {
    if (!m_state)
        return;
    lua_pushinteger(m_state, aliveCount);
    callModuleFunction("wave", "mark_spawned", 1, 0);
}

void ScriptSystem::bossReset() {
    if (!m_state)
        return;
    callModuleFunction("boss", "reset", 0, 0);
}

BossTickResult ScriptSystem::bossTick(int health, int maxHealth, float dt, const glm::vec3& bossPos,
                                       const glm::vec3& playerPos, uint32_t bossBodyId) {
    BossTickResult out{};
    if (!m_state)
        return out;
    lua_pushinteger(m_state, health);
    lua_pushinteger(m_state, maxHealth);
    lua_pushnumber(m_state, static_cast<lua_Number>(dt));
    lua::pushUserdata<glm::vec3>(m_state, bossPos);
    lua::pushUserdata<glm::vec3>(m_state, playerPos);
    lua_pushinteger(m_state, static_cast<lua_Integer>(bossBodyId));
    callModuleFunction("boss", "tick", 6, 3);
    out.phase           = static_cast<int>(lua_tointeger(m_state, -3));
    out.vulnerableTimer = static_cast<float>(lua_tonumber(m_state, -2));
    out.pattern         = static_cast<int>(lua_tointeger(m_state, -1));
    lua_pop(m_state, 3);
    return out;
}

} // namespace ds
