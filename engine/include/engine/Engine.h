#pragma once

#include "engine/AudioSystem.h"
#include "engine/Camera.h"
#include "engine/ParticleSystem.h"
#include "engine/PhysicsWorld.h"
#include "engine/PlayerController.h"
#include "engine/ScriptSystem.h"
#include "engine/TextureManager.h"
#include "engine/UISystem.h"
#include "engine/Vertex.h"
#include "engine/WaveSystem.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/EnemySystem.h"
#include "engine/ecs/RenderSystem.h"

#include <SDL3/SDL.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string_view>
#include <vector>

#include "rhi/IRHIDevice.h"

namespace ds {

struct EngineConfig {
    std::string_view title = "DoomScroller";
    int width              = 1280;
    int height             = 720;
};

class Engine {
  public:
    explicit Engine(const EngineConfig& cfg);
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    void run();

    rhi::IRHIDevice& device() { return *m_device; }
    entt::registry& world() { return m_world; }

  private:
    void processEvents();
    void update();
    void render();
    void initScene();
    void buildArena(rhi::RHITexture albedo, rhi::RHISampler sampler);
    // Loads a .dslv level into the world; returns false if the file is missing
    // or unreadable so the caller can fall back to buildArena().
    bool loadLevel(const char* relPath, rhi::RHITexture albedo, rhi::RHISampler sampler);
    entt::entity spawnEnemy(glm::vec3 position, rhi::RHITexture albedo, rhi::RHISampler sampler);
    void fireWeapon();
    void spawnEnemies();
    void respawnPlayer();

    // --- Game state / wave flow (task 18). ---------------------------------
    // Per-frame gameplay tick, only run while m_state == Playing.
    void updatePlaying();
    // Counts live enemies, registers kills (score/combo), and drives the
    // wave -> intermission -> next-wave loop; transitions to Victory on clear.
    void updateWaves();
    // Spawns the next wave's enemies from SpawnPoint entities (or arena
    // corners when none exist), escalating count per wave.
    void spawnWaveEnemies(int count);
    // Collects SpawnPoint entity positions, falling back to arena corners when
    // the world has none. Used to place each wave.
    std::vector<glm::vec3> waveSpawnPositions() const;
    // Full reset for (re)starting a run: clears the world, rebuilds the level,
    // respawns the player and resets waves/score, then enters Playing.
    void startGame();
    // Screen-space Menu / Dead / Victory overlays (title + score + prompt).
    void renderStateOverlay(rhi::IRHICommandList* cmd);
    // Records the just-finished run's score to the persistent high score file.
    void recordHighScore();
    void initParticles();
    void renderParticles(rhi::IRHICommandList* cmd, const glm::mat4& viewProj);
    // Spawns a short-lived light (muzzle flash, explosion, projectile glow) that
    // is merged into the per-frame light buffer until its lifetime expires.
    void spawnTransientLight(const glm::vec3& position, const glm::vec3& color, float radius, float intensity,
                             float lifetime);
    // Ages transient lights and gathers static LightComponents + transients into
    // m_lightBuffer for upload to the mesh fragment shader.
    void updateLights(float dt);
    // Builds the 2D HUD batch (crosshair, health bar, ammo/wave readout, damage
    // flash) then records it as a screen-space overlay pass.
    void renderHUD(rhi::IRHICommandList* cmd);

    SDL_Window* m_window = nullptr;
    std::unique_ptr<rhi::IRHIDevice> m_device;
    bool m_running = false;

    rhi::RHIShader m_meshVS         = {};
    rhi::RHIShader m_meshFS         = {};
    rhi::RHIPipeline m_meshPipeline = {};
    rhi::RHITexture m_depthTexture  = {};
    rhi::RHISampler m_linearSampler = {};

    // Particle VFX: shared shaders, two pipelines (alpha + additive blend), and
    // a per-frame instance buffer re-uploaded each frame.
    ParticleSystem m_particles;
    rhi::RHIShader m_particleVS                = {};
    rhi::RHIShader m_particleFS                = {};
    rhi::RHIPipeline m_particleAlphaPipe       = {};
    rhi::RHIPipeline m_particleAdditivePipe    = {};
    rhi::RHIBuffer m_particleInstanceBuffer    = {};
    static constexpr uint32_t kMaxParticlesGPU = ParticleSystem::kMaxParticles;

    // 2D HUD / UI overlay: immediate-mode batched quads + bitmap text drawn
    // after the 3D scene with alpha blending and no depth test.
    UISystem m_ui;

    // Forward lighting: persistent LightComponents in the world plus a list of
    // transient lights (muzzle flash, explosions, projectile glow) that decay
    // over time. Both are gathered into m_lightBuffer each frame and pushed to
    // the mesh fragment shader.
    struct TransientLight {
        glm::vec3 position{0.f};
        glm::vec3 color{1.f};
        float radius    = 4.f;
        float intensity = 2.f;
        float life      = 0.f; // remaining seconds
        float maxLife   = 0.f; // initial lifetime, for fade-out
    };
    std::vector<TransientLight> m_transientLights;
    LightBuffer m_lightBuffer;

    std::unique_ptr<TextureManager> m_textures;
    AudioSystem m_audio;
    PhysicsWorld m_physics;

    // Lua scripting host: data-drives wave/enemy tuning from assets/scripts and
    // fires onWaveStart / onEnemyDeath / onPlayerDeath callbacks. Initialised in
    // initScene(); all hooks are no-ops if the config failed to load.
    ScriptSystem m_scripts;
    // Cached enemy stats from the loaded Lua config (or hardcoded defaults when
    // the script provided none). Applied to each spawned enemy.
    ScriptEnemyStats m_enemyStats;
    // Loads assets/scripts/waves.lua (if present), wires bindings, and pulls the
    // wave/enemy tuning back into m_waveConfig / m_enemyStats. Graceful fallback
    // to hardcoded defaults when the file is missing or errors.
    void initScripts();
    static constexpr const char* kWaveScript = "scripts/waves.lua";
    uint32_t m_playerBodyId                  = 0;
    std::unique_ptr<PlayerController> m_player;

    // Weapon loadout: a fixed set of weapons (hitscan + projectile types) the
    // player cycles through. m_weaponIndex selects the active one; m_weapons is
    // never empty after initScene(). currentWeapon() is the firing source.
    std::vector<WeaponComponent> m_weapons;
    size_t m_weaponIndex = 0;
    WeaponComponent& currentWeapon() { return m_weapons[m_weaponIndex]; }
    // Spawns a moving projectile entity for the active weapon (Rocket/Plasma).
    void spawnProjectile(const WeaponComponent& weapon, const glm::vec3& origin, const glm::vec3& dir);

    // Shared material for projectile meshes (reuses the scene albedo/sampler).
    rhi::RHITexture m_projectileAlbedo  = {};
    rhi::RHISampler m_projectileSampler = {};

    bool m_firePressed = false;
    // Edge-detect the dash key (Shift): dash fires on the press frame only.
    bool m_dashHeldPrev = false;
    entt::registry m_world;

    entt::entity m_playerEntity = entt::null;
    float m_playerIFrames       = 0.f; // invulnerability timer (seconds)
    bool m_playerDead           = false;
    float m_respawnTimer        = 0.f; // counts down to respawn after death

    // HUD state. m_damageFlash decays from kDamageFlashTime to 0 after a hit and
    // drives the red full-screen flash; m_timeAccum feeds the low-health pulse.
    float m_damageFlash = 0.f;
    float m_timeAccum   = 0.f;

    static constexpr float kDamageFlashTime = 0.5f;

    // --- Game state machine + wave/score state (task 18). ------------------
    GameState m_state = GameState::Menu;
    WaveState m_wave;
    WaveConfig m_waveConfig;
    int m_highScore = 0; // best score loaded from disk, updated on death/victory
    // Cached albedo/sampler for materials built outside initScene (waves use
    // m_enemyAlbedo/m_enemySampler; the level rebuild on restart reuses these).
    rhi::RHITexture m_sceneAlbedo  = {};
    rhi::RHISampler m_sceneSampler = {};

    // Cached for enemy respawn after player death.
    rhi::RHITexture m_enemyAlbedo  = {};
    rhi::RHISampler m_enemySampler = {};

    // Level file to load on startup, relative to ds::paths::assets(). If it is
    // missing or fails to parse, the engine falls back to the hardcoded arena.
    static constexpr const char* kStartupLevel = "levels/arena.dslv";

    static constexpr glm::vec3 kPlayerSpawn{0.f, 1.7f, 0.f};
    static constexpr float kRespawnDelay  = 2.f;
    static constexpr int kPlayerMaxHealth = 100;

    // Audio asset paths, relative to ds::paths::assets(). Missing files are
    // logged and skipped by AudioSystem (see assets/sfx/README.md).
    static constexpr const char* kSfxWeaponFire = "sfx/weapon_fire.wav";
    static constexpr const char* kSfxEnemyHit   = "sfx/enemy_hit.wav";
    static constexpr const char* kSfxEnemyDeath = "sfx/enemy_death.wav";
    static constexpr const char* kSfxPlayerHit  = "sfx/player_hit.wav";
    static constexpr const char* kSfxDash       = "sfx/dash.wav";
    static constexpr const char* kSfxSlide      = "sfx/slide.wav";
    static constexpr const char* kMusicTrack    = "music/theme.mp3";

    Camera m_camera;
    int m_windowWidth  = 1280;
    int m_windowHeight = 720;

    uint64_t m_lastTick = 0;
    float m_dt          = 0.f;
};

} // namespace ds
