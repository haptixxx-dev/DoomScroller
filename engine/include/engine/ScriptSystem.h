#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// Forward-declare the Lua state so this header pulls in no Lua headers; the
// implementation owns the only include of <lua.hpp>. Keeps the binding layer
// thin and lets callers link against the engine without seeing Lua types.
struct lua_State;

namespace ds {

// Optional, data-driven overrides for enemy stats and wave escalation, read
// back from a loaded Lua config. Mirrors the relevant fields of WaveConfig and
// EnemyComponent without depending on those headers, so ScriptSystem stays a
// standalone, testable unit (tests link only this + Lua).
struct ScriptEnemyStats {
    int health    = 100;
    float speed   = 3.f;
    int damage    = 10;
    bool overrode = false; // true if the config actually set any of these
};

struct ScriptWaveConfig {
    int baseEnemies        = 3;
    int enemiesPerWave     = 2;
    int maxEnemiesPerWave  = 24;
    int maxWaves           = 8;
    float intermissionTime = 3.f;
    int killScore          = 100;
    bool overrode          = false; // true if set_wave_config / the config set any of these
};

// Lua scripting host. Owns a lua_State, exposes a thin "ds" table of C bindings
// to scripts, runs a data-driven config (assets/scripts/waves.lua) and invokes
// optional Lua event callbacks (onWaveStart / onEnemyDeath / onPlayerDeath)
// from C++. Every public method is a no-op-friendly graceful fallback: if no
// state is open or a script is missing, calls quietly do nothing so gameplay
// code never has to guard them.
//
// The engine wires gameplay actions to scripts via the callbacks below
// (spawnEnemy / setWaveConfig / get/setEntityField / emitEvent). ScriptSystem
// never touches the ECS or physics directly, which keeps it testable in
// isolation and the binding layer thin.
class ScriptSystem {
  public:
    // Backs ds.Global.camera's read/write properties (engine/script/LuaGlobal.*).
    // Each getter/setter is wired in Engine::initScripts() to read/write the
    // live ds::Camera directly, so a script write is visible the same frame.
    struct CameraCallbacks {
        std::function<glm::vec3()> getPosition;
        std::function<void(const glm::vec3&)> setPosition;
        std::function<float()> getYaw;
        std::function<void(float)> setYaw;
        std::function<float()> getPitch;
        std::function<void(float)> setPitch;
        std::function<float()> getFovY;
        std::function<void(float)> setFovY;
    };

    // Backs ds.Global.player. Only `health` is writable; the rest are read-only
    // (no setter exists, so ds.Global.player's __newindex rejects those keys).
    struct PlayerCallbacks {
        std::function<int()> getHealth;
        std::function<void(int)> setHealth;
        std::function<int()> getMaxHealth;
        std::function<int()> getDashCharges;
        std::function<bool()> isSliding;
        std::function<float()> getIFrames;
        std::function<glm::vec3()> getEyePosition;
    };

    // Backs ds.Global.time. Entirely read-only.
    struct TimeCallbacks {
        std::function<float()> getDt;
        std::function<float()> getElapsed;
    };

    // C++ side hooks the script can drive. Any unset callback makes the matching
    // Lua binding a silent no-op (still callable, returns sensible defaults).
    struct Callbacks {
        // ds.spawn_enemy(x, y, z [, type]) -> entity id (0 if unhandled).
        std::function<uint32_t(float x, float y, float z, int type)> spawnEnemy;
        // ds.get_field(entity, "health"|"speed"|...) -> number.
        std::function<float(uint32_t entity, std::string_view field)> getEntityField;
        // ds.set_field(entity, "health"|"speed"|..., value).
        std::function<void(uint32_t entity, std::string_view field, float value)> setEntityField;
        // ds.emit_event(name [, number]) -> game-side event sink.
        std::function<void(std::string_view name, double value)> emitEvent;

        CameraCallbacks camera;
        PlayerCallbacks player;
        TimeCallbacks time;
    };

    ScriptSystem();
    ~ScriptSystem();

    ScriptSystem(const ScriptSystem&)            = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    // Creates the lua_State and registers the "ds" binding table. In shipping
    // builds the io/os libraries are NOT opened (sandbox); under DS_DEV they are
    // available for convenience. Returns false (and logs) on allocation failure.
    bool init(const Callbacks& callbacks = {});
    void shutdown();
    bool initialized() const { return m_state != nullptr; }

    // Loads + executes a Lua script file. Returns false (and logs the Lua error)
    // if the file is missing or fails to compile/run, so callers can fall back to
    // hardcoded defaults. Remembers the path for reload().
    bool loadFile(const std::string& path);

    // Executes a Lua source string (used by tests / inline config). Returns false
    // and logs on error.
    bool doString(const std::string& source, const char* chunkName = "=inline");

    // DS_DEV hot-reload: re-runs the last file passed to loadFile(). No-op (false)
    // outside DS_DEV or if no file was loaded.
    bool reload();

    // Reads ds.enemy_stats / ds.wave_config tables (if the config defined them)
    // into plain structs. The returned struct's `overrode` flag is false when the
    // script provided nothing, so the engine keeps its hardcoded defaults.
    ScriptEnemyStats enemyStats() const;
    ScriptWaveConfig waveConfig() const;

    // Invoke optional Lua global event callbacks. Each looks up the named global
    // function; if absent or not callable it is silently skipped. Errors raised
    // inside the callback are caught (pcall) and logged, never propagated.
    void onWaveStart(int wave);
    void onEnemyDeath(uint32_t entity, float x, float y, float z);
    void onPlayerDeath(int finalScore);

    // Raw access for advanced callers / tests.
    lua_State* state() const { return m_state; }

    // Read access to the wired callbacks for non-member binding code (e.g.
    // engine/script/LuaGlobal.cpp) that isn't part of the ScriptSystem class.
    const Callbacks& callbacks() const { return m_callbacks; }

    // Recovers the owning ScriptSystem* from a lua_State* (registry light-
    // userdata lookup). Shared by every binding trampoline file (ScriptSystem's
    // own + script/Lua*.cpp) so the lookup lives in exactly one place.
    static ScriptSystem* fromState(lua_State* L);

    // Reads a global number by name; returns fallback if absent/non-numeric.
    // Convenience used by tests to validate the link + binding plumbing.
    double getGlobalNumber(const char* name, double fallback = 0.0) const;

    // --- Callback dispatch (called by the internal C binding trampolines). ---
    // These forward into m_callbacks, returning sensible defaults when a hook is
    // unset. Public so the file-local trampolines can reach them without a
    // friend dance; not intended for general use.
    uint32_t invokeSpawn(float x, float y, float z, int type);
    float invokeGetField(uint32_t entity, std::string_view field);
    void invokeSetField(uint32_t entity, std::string_view field, float value);
    void invokeEmit(std::string_view name, double value);

  private:
    // Pushes a C function onto the "ds" table under the given key. Used by init.
    void registerBindings();
    // Calls the named global function with `nargs` already on the stack via pcall,
    // logging any error. Pops the function+args. No-op if the global isn't a fn.
    bool callGlobal(const char* name, int nargs);

    lua_State* m_state = nullptr;
    std::string m_lastFile;
    Callbacks m_callbacks;
};

} // namespace ds
