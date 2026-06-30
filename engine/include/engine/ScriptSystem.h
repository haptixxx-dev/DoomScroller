#pragma once

#include "engine/WaveSystem.h"

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

// Result of ds.pickups.register_kill() (assets/scripts/pickups.lua): whether
// this kill's deterministic cadence drops a pickup, and if so which kind
// (mirrors PickupComponent::Kind: 0=Health, 1=Ammo, 2=DashCharge) and face
// value. Kept as a plain int rather than including ecs/Components.h so
// ScriptSystem stays a standalone, ds_script_tests-testable unit.
struct PickupDrop {
    bool drop = false;
    int kind  = 0;
    int value = 0;
};

// Result of ds.pickups.collect_check(): whether the player is within range of
// a pickup, and if so how much of its face value to actually grant (clamped
// to headroom, e.g. missing HP).
struct PickupCollect {
    bool collected = false;
    int grant      = 0;
};

// Result of ds.boss.tick() (assets/scripts/boss.lua): the boss's current
// phase and remaining vulnerable-window timer. Engine.cpp compares the
// returned phase against the BossComponent's previous one to detect a
// transition (and play the VFX/audio cue) and writes both back onto the
// component.
struct BossTickResult {
    int phase             = 0;
    float vulnerableTimer = 0.f;
    // ds.boss.pattern after this tick. The caller compares it against the
    // BossComponent's previous cached value to detect "did fire this tick"
    // (pattern only increments when a volley/burst is actually fired), since
    // there's no separate boolean flag for that — reusing the existing
    // pattern-tracking field keeps the Lua return shape minimal.
    int pattern = 0;
};

// Result of ds.enemy_ai.tick() (assets/scripts/enemy_ai.lua): a per-entity AI
// decision the caller (engine/src/ecs/EnemySystem.cpp) applies to physics/
// component state. `state` mirrors EnemyComponent::State's int value.
// `setVelocity`/`moveIntent` together replace a direct physics call: when
// setVelocity is true the caller sets linear velocity to
// `dir * (lunge ? chargeSpeed : moveSpeed) * moveIntent` (Y untouched);
// when false the caller leaves velocity alone entirely (some original
// branches intentionally never call setLinearVelocity, letting the body
// coast). `lunge`/`fireProjectile`/`meleeAttack`/`armWindup` are one-shot
// per-archetype action flags the caller applies it own way (damage/spawn/
// cooldown rearm), since those touch C++-only state (i-frames, the
// projectile spawn callback) the Lua side can't see.
struct EnemyAIDecision {
    int state           = 0;
    bool setVelocity    = false;
    float moveIntent    = 0.f;
    bool lunge          = false;
    bool fireProjectile = false;
    bool meleeAttack    = false;
    bool armWindup      = false;
};

// Lua scripting host. Owns a lua_State, exposes a thin "ds" table of C bindings
// to scripts, owns the wave/parry/pickups/boss/enemy-AI gameplay state
// machines (assets/scripts/*.lua), data-drives enemy stat overrides, and
// invokes optional Lua event callbacks (onWaveStart / onEnemyDeath /
// onPlayerDeath) from C++. Every public method is a no-op-friendly graceful
// fallback: if no state is open or a script is missing, calls quietly do
// nothing so gameplay code never has to guard them.
//
// The engine wires gameplay actions to scripts via the callbacks below
// (spawnEnemy / spawnProjectile / get/setEntityField / emitEvent / camera /
// player / time) and reads back live gameplay state via the typed wrappers
// (wave*/parry*/pickup*/boss*/enemyAITick). ScriptSystem never touches the
// ECS or physics directly, which keeps it testable in isolation.
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
        // ds.spawn_projectile(origin, velocity, damage, owner_body_id) -> spawns
        // an enemy/boss-owned projectile entity (used by the boss attack
        // patterns in assets/scripts/boss.lua).
        std::function<void(const glm::vec3& origin, const glm::vec3& velocity, int damage, uint32_t ownerBodyId)>
            spawnProjectile;

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

    // Reads the ds.enemy_stats table (if the config defined it) into a plain
    // struct. The returned struct's `overrode` flag is false when the script
    // provided nothing, so the engine keeps its hardcoded defaults.
    ScriptEnemyStats enemyStats() const;

    // Invoke optional Lua global event callbacks. Each looks up the named global
    // function; if absent or not callable it is silently skipped. Errors raised
    // inside the callback are caught (pcall) and logged, never propagated.
    void onWaveStart(int wave);
    void onEnemyDeath(uint32_t entity, float x, float y, float z);
    void onPlayerDeath(int finalScore);

    // --- Parry state machine (assets/scripts/parry.lua, module ds.parry). ---
    // Lua-side port of ParryTech.h's pure functions; Engine.cpp drives these
    // instead of calling the (now-unused-by-Engine) C++ free functions. Every
    // call is a no-op-friendly graceful fallback if parry.lua failed to load.
    void parryReset();
    void parryTrigger();
    void parryTick(float dt);
    bool parryActive() const;
    glm::vec3 parryReflect(const glm::vec3& incoming, float speedBoost = 1.5f) const;
    // Reads ds.parry.tuning.dash_refund; falls back to 1.f if parry.lua/the
    // field is missing.
    float parryDashRefund() const;

    // --- Pickups (assets/scripts/pickups.lua, module ds.pickups). -----------
    // Lua owns the drop-cadence/kind-cycling decision and the in-range +
    // headroom-clamp decision; Engine.cpp still owns spawning the pickup
    // entity and applying the granted HP/ammo/dash-charge (component mutation
    // stays C++, per the migration brief).
    PickupDrop pickupRegisterKill();
    PickupCollect pickupCollectCheck(const glm::vec3& playerPos, const glm::vec3& pickupPos, float radius, int value,
                                      int headroom) const;
    // Resets ds.pickups.kill_count (the drop-cadence counter) to 0; called on
    // (re)start so wave-to-wave drop cadence doesn't carry over between runs.
    void pickupsReset();

    // --- Wave progression (assets/scripts/wave.lua, module ds.wave). --------
    // Lua owns the live WaveState; readWaveState() pulls ds.wave.state back
    // into a WaveState for Engine.h/HUD/save code that reads m_wave.* fields
    // directly. Call it after any of the mutators below to refresh the cache.
    WaveState readWaveState() const;
    void waveReset();
    void waveTick(float dt);
    void waveRegisterKill();
    void waveAdvance();
    int waveEnemiesForWave(int wave) const;
    // Mirrors EnTT's live enemy count into ds.wave.state.alive_enemies.
    void waveSetAliveEnemies(int n);
    // First clear frame: arms the intermission countdown to the next wave.
    void waveArmIntermission();
    // Records that aliveCount enemies were just spawned and clears
    // ds.wave.state.spawn_pending.
    void waveMarkSpawned(int aliveCount);

    // --- Boss (assets/scripts/boss.lua, module ds.boss). ---------------------
    // Lua owns phase-threshold computation and attack-pattern selection/
    // cadence (firing pellets itself via ds.spawn_projectile); Engine.cpp keeps
    // the EnTT/Jolt position sync and the entire death-handling block.
    void bossReset();
    BossTickResult bossTick(int health, int maxHealth, float dt, const glm::vec3& bossPos, const glm::vec3& playerPos,
                             uint32_t bossBodyId);

    // --- Enemy AI (assets/scripts/enemy_ai.lua, module ds.enemy_ai). --------
    // Lua owns the FSM decision (state transitions, attack/lunge/fire timing);
    // engine/src/ecs/EnemySystem.cpp keeps the EnTT/Jolt loop and applies the
    // decision (velocity, damage, projectile spawn — all C++-only state).
    // archetype/state are plain ints mirroring EnemyArchetype/
    // EnemyComponent::State so this stays decoupled from Components.h.
    EnemyAIDecision enemyAITick(int archetype, int state, float dist, float attackCooldown, float moveSpeed,
                                 float attackRange, float detectionRange, float attackInterval, float chargeWindup,
                                 float chargeSpeed);
    // Deterministic archetype pick for a wave-spawned enemy (port of
    // Engine::archetypeForWave); returns 0=Grunt, 1=Charger, 2=Ranged.
    int archetypeForWave(int wave, int spawnIndex) const;

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
    void invokeSpawnProjectile(const glm::vec3& origin, const glm::vec3& velocity, int damage, uint32_t ownerBodyId);

  private:
    // Pushes a C function onto the "ds" table under the given key. Used by init.
    void registerBindings();
    // Calls the named global function with `nargs` already on the stack via pcall,
    // logging any error. Pops the function+args. No-op if the global isn't a fn.
    bool callGlobal(const char* name, int nargs);
    // Calls ds.<moduleField>.<fn>(...) via pcall. Caller has already pushed
    // `nargs` arguments on the stack. On success leaves `nresults` values on
    // the stack and returns true. On any failure (ds.<moduleField> missing/not
    // a table, the function missing/not callable, or a Lua error) pushes
    // `nresults` nils instead (logging only the Lua-error case, mirroring
    // callGlobal's quiet skip for an absent function) and returns false.
    // Shared by every per-system typed wrapper (parry/wave/boss/pickups/enemy
    // AI) so the dispatch lives in exactly one place.
    bool callModuleFunction(const char* moduleField, const char* fn, int nargs, int nresults) const;

    lua_State* m_state = nullptr;
    std::string m_lastFile;
    Callbacks m_callbacks;
};

} // namespace ds
