#include "engine/ScriptSystem.h"

#include "engine/script/LuaGlobal.h"
#include "engine/script/LuaVec3.h"

#include <SDL3/SDL_log.h>

#include <lua.hpp>

namespace ds {

namespace {

// Registry key under which we stash the owning ScriptSystem* so the C binding
// trampolines can reach the C++ callbacks. Address-as-key is the idiomatic Lua
// pattern for a unique light-userdata registry slot.
char kSelfKey = 0;

} // namespace

ScriptSystem* ScriptSystem::fromState(lua_State* L) {
    lua_pushlightuserdata(L, &kSelfKey);
    lua_gettable(L, LUA_REGISTRYINDEX);
    auto* self = static_cast<ScriptSystem*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return self;
}

namespace {

ScriptSystem* selfFromState(lua_State* L) {
    return ScriptSystem::fromState(L);
}

// --- ds.* binding trampolines ------------------------------------------------

// ds.spawn_enemy(x, y, z [, type]) -> entity id
int l_spawn_enemy(lua_State* L) {
    ScriptSystem* self = selfFromState(L);
    float x            = static_cast<float>(luaL_checknumber(L, 1));
    float y            = static_cast<float>(luaL_checknumber(L, 2));
    float z            = static_cast<float>(luaL_checknumber(L, 3));
    int type           = static_cast<int>(luaL_optinteger(L, 4, 0));
    uint32_t id        = 0;
    if (self && self->state())
        id = self->invokeSpawn(x, y, z, type);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

int l_get_field(lua_State* L) {
    ScriptSystem* self = selfFromState(L);
    uint32_t entity    = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* field  = luaL_checkstring(L, 2);
    float value        = 0.f;
    if (self && self->state())
        value = self->invokeGetField(entity, field);
    lua_pushnumber(L, value);
    return 1;
}

int l_set_field(lua_State* L) {
    ScriptSystem* self = selfFromState(L);
    uint32_t entity    = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* field  = luaL_checkstring(L, 2);
    float value        = static_cast<float>(luaL_checknumber(L, 3));
    if (self && self->state())
        self->invokeSetField(entity, field, value);
    return 0;
}

int l_emit_event(lua_State* L) {
    ScriptSystem* self = selfFromState(L);
    const char* name   = luaL_checkstring(L, 1);
    double value       = luaL_optnumber(L, 2, 0.0);
    if (self && self->state())
        self->invokeEmit(name, value);
    return 0;
}

// ds.spawn_projectile(origin, velocity, damage, owner_body_id) -> spawns an
// enemy/boss-owned projectile entity (used by boss.lua's attack patterns).
int l_spawn_projectile(lua_State* L) {
    ScriptSystem* self   = selfFromState(L);
    glm::vec3* origin    = ds::lua::checkUserdata<glm::vec3>(L, 1);
    glm::vec3* velocity  = ds::lua::checkUserdata<glm::vec3>(L, 2);
    int damage           = static_cast<int>(luaL_checkinteger(L, 3));
    uint32_t ownerBodyId = static_cast<uint32_t>(luaL_checkinteger(L, 4));
    if (self && self->state())
        self->invokeSpawnProjectile(*origin, *velocity, damage, ownerBodyId);
    return 0;
}

// ds.level.add_box(center, half_extents, color) — accumulates a box into the
// level-gen LevelData (engine/LevelGen.h). Silently no-ops outside a level-gen
// run, since Callbacks::levelAddBox is unset on the main gameplay ScriptSystem.
int l_level_add_box(lua_State* L) {
    ScriptSystem* self = selfFromState(L);
    glm::vec3* center  = ds::lua::checkUserdata<glm::vec3>(L, 1);
    glm::vec3* half    = ds::lua::checkUserdata<glm::vec3>(L, 2);
    glm::vec3* color   = ds::lua::checkUserdata<glm::vec3>(L, 3);
    if (self && self->state())
        self->invokeLevelAddBox(*center, *half, *color);
    return 0;
}

// ds.level.add_mesh(path, position [, euler_degrees]) — euler_degrees is an
// optional Vec3 of Euler angles in degrees (ergonomic for level authors,
// who think in degrees, not quaternions); converted here so the C++
// callback stays in proper glm::quat form.
int l_level_add_mesh(lua_State* L) {
    ScriptSystem* self = selfFromState(L);
    const char* path   = luaL_checkstring(L, 1);
    glm::vec3* pos     = ds::lua::checkUserdata<glm::vec3>(L, 2);
    glm::vec3 eulerDeg{0.f};
    if (!lua_isnoneornil(L, 3))
        eulerDeg = *ds::lua::checkUserdata<glm::vec3>(L, 3);
    if (self && self->state())
        self->invokeLevelAddMesh(path, *pos, glm::quat(glm::radians(eulerDeg)));
    return 0;
}

// ds.level.add_spawn(position, is_player [, archetype]) — archetype is an
// optional int (0=Grunt, 1=Charger, 2=Ranged) hinting which enemy type this
// spawn should favor; omit (or nil) for the engine's default wave/index-based
// selection. Ignored when is_player is true.
int l_level_add_spawn(lua_State* L) {
    ScriptSystem* self = selfFromState(L);
    glm::vec3* pos      = ds::lua::checkUserdata<glm::vec3>(L, 1);
    bool isPlayer       = lua_toboolean(L, 2) != 0;
    int archetypeHint   = lua_isnoneornil(L, 3) ? -1 : static_cast<int>(luaL_checkinteger(L, 3));
    if (self && self->state())
        self->invokeLevelAddSpawn(*pos, isPlayer, archetypeHint);
    return 0;
}

// ds.level.add_light(position, color, radius, intensity)
int l_level_add_light(lua_State* L) {
    ScriptSystem* self = selfFromState(L);
    glm::vec3* pos      = ds::lua::checkUserdata<glm::vec3>(L, 1);
    glm::vec3* color    = ds::lua::checkUserdata<glm::vec3>(L, 2);
    float radius        = static_cast<float>(luaL_checknumber(L, 3));
    float intensity     = static_cast<float>(luaL_checknumber(L, 4));
    if (self && self->state())
        self->invokeLevelAddLight(*pos, *color, radius, intensity);
    return 0;
}

// Reads an integer field from a table at `tableIdx`; leaves the table in place.
int tableIntField(lua_State* L, int tableIdx, const char* key, int fallback, bool* found) {
    lua_getfield(L, tableIdx, key);
    int out = fallback;
    if (lua_isnumber(L, -1)) {
        out = static_cast<int>(lua_tointeger(L, -1));
        if (found)
            *found = true;
    }
    lua_pop(L, 1);
    return out;
}

float tableFloatField(lua_State* L, int tableIdx, const char* key, float fallback, bool* found) {
    lua_getfield(L, tableIdx, key);
    float out = fallback;
    if (lua_isnumber(L, -1)) {
        out = static_cast<float>(lua_tonumber(L, -1));
        if (found)
            *found = true;
    }
    lua_pop(L, 1);
    return out;
}

} // namespace

uint32_t ScriptSystem::invokeSpawn(float x, float y, float z, int type) {
    return m_callbacks.spawnEnemy ? m_callbacks.spawnEnemy(x, y, z, type) : 0u;
}

float ScriptSystem::invokeGetField(uint32_t entity, std::string_view field) {
    return m_callbacks.getEntityField ? m_callbacks.getEntityField(entity, field) : 0.f;
}

void ScriptSystem::invokeSetField(uint32_t entity, std::string_view field, float value) {
    if (m_callbacks.setEntityField)
        m_callbacks.setEntityField(entity, field, value);
}

void ScriptSystem::invokeEmit(std::string_view name, double value) {
    if (m_callbacks.emitEvent)
        m_callbacks.emitEvent(name, value);
}

void ScriptSystem::invokeSpawnProjectile(const glm::vec3& origin, const glm::vec3& velocity, int damage,
                                          uint32_t ownerBodyId) {
    if (m_callbacks.spawnProjectile)
        m_callbacks.spawnProjectile(origin, velocity, damage, ownerBodyId);
}

void ScriptSystem::invokeLevelAddBox(glm::vec3 center, glm::vec3 halfExtents, glm::vec3 color) {
    if (m_callbacks.levelAddBox)
        m_callbacks.levelAddBox(center, halfExtents, color);
}

void ScriptSystem::invokeLevelAddMesh(const std::string& meshPath, glm::vec3 position, glm::quat rotation) {
    if (m_callbacks.levelAddMesh)
        m_callbacks.levelAddMesh(meshPath, position, rotation);
}

void ScriptSystem::invokeLevelAddSpawn(glm::vec3 position, bool isPlayerStart, int archetypeHint) {
    if (m_callbacks.levelAddSpawn)
        m_callbacks.levelAddSpawn(position, isPlayerStart, archetypeHint);
}

void ScriptSystem::invokeLevelAddLight(glm::vec3 position, glm::vec3 color, float radius, float intensity) {
    if (m_callbacks.levelAddLight)
        m_callbacks.levelAddLight(position, color, radius, intensity);
}

ScriptSystem::ScriptSystem() = default;

ScriptSystem::~ScriptSystem() {
    shutdown();
}

bool ScriptSystem::init(const Callbacks& callbacks) {
    if (m_state)
        shutdown();

    m_callbacks = callbacks;
    m_state     = luaL_newstate();
    if (!m_state) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ScriptSystem: failed to create Lua state");
        return false;
    }

    // Open a curated set of standard libraries. The io/os libraries are a
    // sandbox risk (file + process access), so only expose them under DS_DEV.
    luaL_requiref(m_state, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(m_state, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(m_state, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(m_state, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(m_state, 4);
#ifdef DS_DEV
    luaL_requiref(m_state, LUA_OSLIBNAME, luaopen_os, 1);
    luaL_requiref(m_state, LUA_IOLIBNAME, luaopen_io, 1);
    lua_pop(m_state, 2);
#endif

    // Stash `this` so trampolines can reach the callbacks.
    lua_pushlightuserdata(m_state, &kSelfKey);
    lua_pushlightuserdata(m_state, this);
    lua_settable(m_state, LUA_REGISTRYINDEX);

    ds::lua::registerVec3(m_state);
    registerBindings();
    ds::lua::registerGlobalTable(m_state); // needs the "ds" table registerBindings() just built
    return true;
}

void ScriptSystem::shutdown() {
    if (m_state) {
        lua_close(m_state);
        m_state = nullptr;
    }
}

void ScriptSystem::registerBindings() {
    // Build the global "ds" table of bindings.
    lua_newtable(m_state);

    lua_pushcfunction(m_state, l_spawn_enemy);
    lua_setfield(m_state, -2, "spawn_enemy");
    lua_pushcfunction(m_state, l_get_field);
    lua_setfield(m_state, -2, "get_field");
    lua_pushcfunction(m_state, l_set_field);
    lua_setfield(m_state, -2, "set_field");
    lua_pushcfunction(m_state, l_emit_event);
    lua_setfield(m_state, -2, "emit_event");
    lua_pushcfunction(m_state, l_spawn_projectile);
    lua_setfield(m_state, -2, "spawn_projectile");

    // ds.level.* — one-shot level-generation hooks (engine/LevelGen.h).
    lua_newtable(m_state);
    lua_pushcfunction(m_state, l_level_add_box);
    lua_setfield(m_state, -2, "add_box");
    lua_pushcfunction(m_state, l_level_add_mesh);
    lua_setfield(m_state, -2, "add_mesh");
    lua_pushcfunction(m_state, l_level_add_spawn);
    lua_setfield(m_state, -2, "add_spawn");
    lua_pushcfunction(m_state, l_level_add_light);
    lua_setfield(m_state, -2, "add_light");
    lua_setfield(m_state, -2, "level"); // ds.level = {...}

    lua_setglobal(m_state, "ds");
}

bool ScriptSystem::loadFile(const std::string& path) {
    if (!m_state)
        return false;
    m_lastFile = path;
    if (luaL_dofile(m_state, path.c_str()) != LUA_OK) {
        const char* err = lua_tostring(m_state, -1);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ScriptSystem: failed to run '%s': %s", path.c_str(),
                    err ? err : "(unknown error)");
        lua_pop(m_state, 1);
        return false;
    }
    return true;
}

bool ScriptSystem::doString(const std::string& source, const char* chunkName) {
    if (!m_state)
        return false;
    if (luaL_loadbuffer(m_state, source.data(), source.size(), chunkName) != LUA_OK ||
        lua_pcall(m_state, 0, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(m_state, -1);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ScriptSystem: doString error: %s", err ? err : "(unknown error)");
        lua_pop(m_state, 1);
        return false;
    }
    return true;
}

bool ScriptSystem::reload() {
#ifdef DS_DEV
    if (m_lastFile.empty())
        return false;
    return loadFile(m_lastFile);
#else
    return false;
#endif
}

ScriptEnemyStats ScriptSystem::enemyStats() const {
    ScriptEnemyStats out{};
    if (!m_state)
        return out;
    lua_getglobal(m_state, "ds");
    if (!lua_istable(m_state, -1)) {
        lua_pop(m_state, 1);
        return out;
    }
    lua_getfield(m_state, -1, "enemy_stats");
    if (lua_istable(m_state, -1)) {
        int idx      = lua_gettop(m_state);
        bool found   = false;
        out.health   = tableIntField(m_state, idx, "health", out.health, &found);
        out.speed    = tableFloatField(m_state, idx, "speed", out.speed, &found);
        out.damage   = tableIntField(m_state, idx, "damage", out.damage, &found);
        out.overrode = found;
    }
    lua_pop(m_state, 2); // enemy_stats + ds
    return out;
}

bool ScriptSystem::callGlobal(const char* name, int nargs) {
    // Stack on entry: nargs args pushed on top. We need the function *below* them,
    // so fetch it then rotate it under the args.
    lua_getglobal(m_state, name);
    if (!lua_isfunction(m_state, -1)) {
        // Not defined: drop the non-function + the args we were given.
        lua_pop(m_state, 1 + nargs);
        return false;
    }
    // Move the function from the top to below the nargs arguments.
    lua_insert(m_state, -(nargs + 1));
    if (lua_pcall(m_state, nargs, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(m_state, -1);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ScriptSystem: error in '%s': %s", name,
                    err ? err : "(unknown error)");
        lua_pop(m_state, 1);
        return false;
    }
    return true;
}

bool ScriptSystem::callModuleFunction(const char* moduleField, const char* fn, int nargs, int nresults) const {
    // Stack on entry: nargs args already pushed on top.
    lua_getglobal(m_state, "ds");
    lua_getfield(m_state, -1, moduleField);
    lua_remove(m_state, -2); // ds; stack: args..., module
    if (!lua_istable(m_state, -1)) {
        lua_pop(m_state, 1 + nargs); // module (non-table) + args
        for (int i = 0; i < nresults; ++i)
            lua_pushnil(m_state);
        return false;
    }
    lua_getfield(m_state, -1, fn);
    lua_remove(m_state, -2); // module table; stack: args..., fn
    if (!lua_isfunction(m_state, -1)) {
        lua_pop(m_state, 1 + nargs); // fn (non-function) + args
        for (int i = 0; i < nresults; ++i)
            lua_pushnil(m_state);
        return false;
    }
    lua_insert(m_state, -(nargs + 1)); // move fn below the args
    if (lua_pcall(m_state, nargs, nresults, 0) != LUA_OK) {
        const char* err = lua_tostring(m_state, -1);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ScriptSystem: error in 'ds.%s.%s': %s", moduleField, fn,
                    err ? err : "(unknown error)");
        lua_pop(m_state, 1);
        for (int i = 0; i < nresults; ++i)
            lua_pushnil(m_state);
        return false;
    }
    return true;
}

void ScriptSystem::onWaveStart(int wave) {
    if (!m_state)
        return;
    lua_pushinteger(m_state, wave);
    callGlobal("onWaveStart", 1);
}

void ScriptSystem::onEnemyDeath(uint32_t entity, float x, float y, float z) {
    if (!m_state)
        return;
    lua_pushinteger(m_state, static_cast<lua_Integer>(entity));
    lua_pushnumber(m_state, x);
    lua_pushnumber(m_state, y);
    lua_pushnumber(m_state, z);
    callGlobal("onEnemyDeath", 4);
}

void ScriptSystem::onPlayerDeath(int finalScore) {
    if (!m_state)
        return;
    lua_pushinteger(m_state, finalScore);
    callGlobal("onPlayerDeath", 1);
}

double ScriptSystem::getGlobalNumber(const char* name, double fallback) const {
    if (!m_state)
        return fallback;
    lua_getglobal(m_state, name);
    double out = lua_isnumber(m_state, -1) ? lua_tonumber(m_state, -1) : fallback;
    lua_pop(m_state, 1);
    return out;
}

} // namespace ds
