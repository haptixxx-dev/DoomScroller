#pragma once

#include "engine/AudioSystem.h"
#include "engine/Camera.h"
#include "engine/CombatFeedback.h"
#include "engine/GameFeel.h"
#include "engine/ParticleSystem.h"
#include "engine/PhysicsWorld.h"
#include "engine/PlayerController.h"
#include "engine/QualityProfile.h"
#include "engine/RenderGraph.h"
#include "engine/SaveData.h"
#include "engine/ScriptSystem.h"
#include "engine/ShaderWatcher.h"
#include "engine/ShadowMatrix.h"
#include "engine/StyleMeter.h"
#include "engine/TextureManager.h"
#include "engine/UISystem.h"
#include "engine/UserStorage.h"
#include "engine/Vertex.h"
#include "engine/WaveSystem.h"
#include "engine/WeaponUpgrade.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/EnemySystem.h"
#include "engine/ecs/RenderSystem.h"

#include <SDL3/SDL.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
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
    // Registered as an entt on_destroy<MeshComponent> observer so that whenever a
    // mesh-carrying entity is destroyed (projectiles, gibs, enemies, level
    // geometry, or world.clear() on restart/shutdown) its per-entity GPU vertex
    // and index buffers are released. Every MeshComponent owns unique buffers
    // (makeBoxMesh / makeLevelBox / buildArena each create fresh ones, never
    // shared), so freeing on destroy cannot double-free. Without this the engine
    // leaks two GPU buffers per spawned projectile/gib/enemy for the process life.
    void onMeshDestroyed(entt::registry& reg, entt::entity e);
    void buildArena(rhi::RHITexture albedo, rhi::RHISampler sampler);
    // Loads a .dslv level into the world; returns false if the file is missing
    // or unreadable so the caller can fall back to buildArena(). When the level
    // has a player-start spawn (flags bit0), its position is written to
    // `playerStart` so the caller can place the camera + capsule there (task 42).
    bool loadLevel(const char* relPath, rhi::RHITexture albedo, rhi::RHISampler sampler,
                   glm::vec3* playerStart = nullptr);
    entt::entity spawnEnemy(glm::vec3 position, rhi::RHITexture albedo, rhi::RHISampler sampler,
                            EnemyArchetype archetype = EnemyArchetype::Grunt);
    // Picks an archetype for a wave-spawned enemy, mixing in Chargers/Ranged as
    // waves escalate (deterministic from wave + spawn index, no RNG state).
    EnemyArchetype archetypeForWave(int wave, int spawnIndex) const;
    // Primary fire (LMB): hitscan ray or a single projectile, using the modded
    // weapon. Alt fire (RMB): shotgun spread / buffed single shot.
    void fireWeaponPrimary(const WeaponComponent& weapon);
    void fireWeaponAlt(const WeaponComponent& weapon);
    // Resolves one hitscan ray (origin/dir) for the modded weapon, applying
    // damage + spawning a DamageEvent/hit marker on an enemy hit.
    void fireHitscanRay(const WeaponComponent& weapon, const glm::vec3& origin, const glm::vec3& dir);
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
    std::vector<SpawnPoint> waveSpawnPositions() const;
    // Full reset for (re)starting a run: clears the world, rebuilds the level,
    // respawns the player and resets waves/score, then enters Playing.
    void startGame();
    // Screen-space Menu / Dead / Victory overlays (title + score + prompt).
    void renderStateOverlay(rhi::IRHICommandList* cmd);
    // Settings sub-screen (task 44): volume + look-sensitivity sliders and
    // vsync/fullscreen toggles, drawn over the Menu / pause overlay. Returns the
    // y the caller can continue drawing below (unused today). Hit-testing is in
    // handleSettingsClick / handleSettingsDrag from processEvents.
    void renderSettings(rhi::IRHICommandList* cmd);
    // Routes a mouse press/drag at absolute window pixel (mx,my) onto the
    // settings widgets. `pressed` is true on the initial click (toggles flip
    // once), false for a drag (sliders track). Returns true if a widget consumed
    // the input. Applies the change live + persists to <userDir>/settings.cfg.
    bool handleSettingsInput(float mx, float my, bool pressed);
    // Applies m_settings to the live systems (audio buses + camera sensitivity)
    // and writes them to disk. Called whenever a widget changes a value.
    void applyAndPersistSettings();
    // Layout rect for a settings row, computed from a row index. Shared by the
    // renderer and the hit-tester so they stay in sync.
    struct SettingsRect {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    };
    SettingsRect settingsTrackRect(int row) const;
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

    // --- Quality tier (task 34). -------------------------------------------
    // Selected once at construction from the device's reported RHICaps. Gates
    // render-graph passes + resource sizes: skip the shadow pass + shadow
    // sampling setup when !m_quality.shadows, size the shadow map from
    // m_quality.shadowMapSize, and pick the bloom RT size from
    // m_quality.halfResBloom. computeParticles is stored for the particle path.
    // Behaviour is identical to the previous hardcoded config on the Enhanced
    // tier (shadows on, full-res bloom, 2048 shadow map).
    QualityProfile m_quality;

    rhi::RHIShader m_meshVS         = {};
    rhi::RHIShader m_meshFS         = {};
    rhi::RHIPipeline m_meshPipeline = {};
    rhi::RHITexture m_depthTexture  = {};
    rhi::RHISampler m_linearSampler = {};

    // Per-frame GPU instance buffer (task 28): renderSystem + renderDepthOnly
    // upload every visible entity's model matrix here, then issue one instanced
    // drawIndexed per mesh+albedo batch. Sized for kMaxInstances mat4s and
    // re-uploaded each frame (a vertex buffer; binding slot 1, instanced).
    rhi::RHIBuffer m_instanceBuffer = {};
    // Separate instance buffer for the shadow pass (task 28). The shadow pass
    // groups by geometry alone (albedo-agnostic) so its batch order — and thus
    // the per-batch matrix offsets — differs from the scene pass. They run in
    // the same recorded command buffer but each uploads via its own immediate
    // copy before submit, so a SHARED buffer would let the scene upload clobber
    // the matrices the already-recorded shadow draws still reference. Two
    // buffers keep each pass reading exactly what it uploaded.
    rhi::RHIBuffer m_shadowInstanceBuffer   = {};
    static constexpr uint32_t kMaxInstances = 4096;

    // --- Single-cascade directional sun shadow map (task 31). --------------
    // A square D32Float depth target rendered from the sun's point of view by
    // m_shadowPipeline (depth-only: no color targets, front-face cull to reduce
    // peter-panning). The mesh fragment shader samples it to shadow the sun
    // term. m_sunLightSpace is rebuilt each frame from ds::sunLightSpaceMatrix
    // over the static arena bounds and pushed to both passes.
    rhi::RHIShader m_shadowVS         = {};
    rhi::RHIShader m_shadowFS         = {};
    rhi::RHIPipeline m_shadowPipeline = {};
    rhi::RHITexture m_shadowMap       = {};
    rhi::RHISampler m_shadowSampler   = {};
    glm::mat4 m_sunLightSpace{1.f};
    // Default/Enhanced-tier shadow map resolution. The ACTUAL size used at
    // runtime is m_shadowMapSize, seeded from m_quality.shadowMapSize in
    // initScene so the Minimum tier can drop to a smaller (or zero) map.
    static constexpr uint32_t kShadowMapSize = 2048;
    // Runtime-selected sizes (task 34). Seeded in initScene from m_quality.
    uint32_t m_shadowMapSize = kShadowMapSize;
    uint32_t m_bloomWidth    = 0; // bloom RT width  (full or half window res)
    uint32_t m_bloomHeight   = 0; // bloom RT height
    // Sun direction shared with mesh.slang's kSunDir (= normalize(0.5,1,0.3)).
    // sunLightSpaceMatrix wants the direction the light TRAVELS, i.e. the
    // negated "toward the sun" vector used in the BRDF.
    static constexpr glm::vec3 kSunDir{0.5f, 1.f, 0.3f};
    // Renders all Transform+Mesh entities depth-only from the sun's POV into
    // m_shadowMap, pushing lightSpace*model as the vertex MVP.
    void renderDepthOnly(rhi::IRHICommandList* cmd, const glm::mat4& lightSpace);

    // --- Point-light shadow (one shadow-casting light per frame, 6 faces). --
    // Distance-based point-light shadow (see engine/ShadowMatrix.h's
    // pointShadowFaceMatrix and shaders/point_shadow_depth.slang): each cube
    // face writes linear distance-from-light into its own small R32Float
    // color target. A single scratch D32Float depth buffer is shared and
    // re-cleared across all 6 face passes purely for each pass's own
    // hidden-surface test (it is never sampled afterward) — safe because the
    // 6 passes run strictly sequentially within one frame. The face textures
    // + sampler + scratch depth are always created (mirrors m_shadowMap's
    // texture, which is unconditional so the mesh FS's fixed binding slots
    // stay valid on every tier); the VS/FS/pipeline are gated by
    // m_quality.shadows exactly like m_shadowVS/m_shadowFS/m_shadowPipeline,
    // since renderPointShadowFace is only ever invoked when drawShadows.
    rhi::RHIShader m_pointShadowVS         = {};
    rhi::RHIShader m_pointShadowFS         = {};
    rhi::RHIPipeline m_pointShadowPipeline = {};
    rhi::RHITexture m_pointShadowFaces[6]{};
    rhi::RHITexture m_pointShadowDepthScratch = {};
    rhi::RHISampler m_pointShadowSampler      = {};
    // One buffer PER FACE rather than a single shared one (mirrors why
    // m_shadowInstanceBuffer is separate from m_instanceBuffer, see that
    // comment above): uploadImmediate submits its copy on its own command
    // buffer, independent of the main recorded command buffer's eventual
    // submit, so two passes recorded back-to-back in the same frame that
    // shared one buffer could have a later face's upload race a not-yet-
    // executed earlier face's draw. Six small buffers (4096 mat4s each,
    // ~1.5MB total) sidestep that entirely.
    rhi::RHIBuffer m_pointShadowInstanceBuffer[6]{};
    static constexpr uint32_t kPointShadowFaceSize = 512;
    // Renders depth (as linear distance from `lightPos`) for cube face
    // `face` into m_pointShadowFaces[face]; the caller has already bound that
    // texture as the active pass's color attachment.
    void renderPointShadowFace(rhi::IRHICommandList* cmd, int face, const glm::mat4& faceLightSpace,
                                const glm::vec3& lightPos);

    // Offscreen HDR target (task 21). Scene + particles render into this
    // RGBA16Float texture; the tonemap pass then resolves it to the LDR
    // swapchain so HDR lighting can exceed 1.0 before the filmic curve. Created
    // window-sized alongside the depth texture (recreated together on resize).
    rhi::RHITexture m_hdrTexture       = {};
    rhi::RHIShader m_tonemapVS         = {};
    rhi::RHIShader m_tonemapFS         = {};
    rhi::RHIPipeline m_tonemapPipeline = {};

    // Bloom post-process (task 26). After Pass A (scene -> HDR), a bright-pass
    // extracts over-threshold light into m_bloomA, then a separable Gaussian
    // ping-pongs A->B (horizontal) and B->A (vertical). The tonemap pass adds
    // m_bloomA back to the HDR sample before the curve. All targets are
    // RGBA16Float, window-sized (full-res first cut), recreated with m_hdrTexture.
    rhi::RHITexture m_bloomA          = {};
    rhi::RHITexture m_bloomB          = {};
    rhi::RHIShader m_brightVS         = {};
    rhi::RHIShader m_brightFS         = {};
    rhi::RHIPipeline m_brightPipeline = {};
    rhi::RHIShader m_blurVS           = {};
    rhi::RHIShader m_blurFS           = {};
    rhi::RHIPipeline m_blurPipeline   = {};
    // Tuning (mirrors BloomKernel.h bright-pass band); intensity scales the
    // additive composite. Visual-only constants, safe to tweak.
    static constexpr float kBloomThreshold = 1.0f;
    static constexpr float kBloomKnee      = 0.5f;
    static constexpr float kBloomIntensity = 0.6f;
    // Runs bright-pass + separable blur after Pass A; leaves the final blurred
    // bloom in m_bloomA for the tonemap composite.
    void renderBloom(rhi::IRHICommandList* cmd);

    // Particle VFX: shared shaders, two pipelines (alpha + additive blend), and
    // a per-frame instance buffer re-uploaded each frame.
    ParticleSystem m_particles;
    rhi::RHIShader m_particleVS                = {};
    rhi::RHIShader m_particleFS                = {};
    rhi::RHIPipeline m_particleAlphaPipe       = {};
    rhi::RHIPipeline m_particleAdditivePipe    = {};
    rhi::RHIBuffer m_particleInstanceBuffer    = {};
    static constexpr uint32_t kMaxParticlesGPU = ParticleSystem::kMaxParticles;

    // --- GPU compute particle sim (task 39, Enhanced tier only). -----------
    // Built in initParticles() only when m_quality.computeParticles AND the
    // compute pipeline compiled. m_computeParticles is the runtime gate the
    // particle dispatch checks; on the Minimum tier (and whenever the pipeline
    // is invalid) it stays false and renderParticles() keeps the CPU upload
    // path. The state buffer holds the per-particle GpuParticle pool (read-only
    // in the shader); the dispatch writes m_particleInstanceBuffer on-GPU,
    // removing the synchronous per-frame instance re-upload.
    rhi::RHIComputePipeline m_particleComputePipe = {};
    rhi::RHIBuffer m_particleStateBuffer          = {};
    bool m_computeParticles                       = false;
    // Per-frame particle bucket counts produced by the compute path: the alpha
    // bucket occupies instances [0, m_computeAlphaCount) and the additive bucket
    // the next m_computeAdditiveCount instances in m_particleInstanceBuffer.
    uint32_t m_computeAlphaCount    = 0;
    uint32_t m_computeAdditiveCount = 0;
    // Uploads the live particle pool to m_particleStateBuffer and dispatches the
    // compute sim to fill m_particleInstanceBuffer on-GPU. Called OUTSIDE the
    // render pass (compute passes can't nest in a render pass). Records the
    // bucket counts so renderParticles() can issue the billboard draws from the
    // GPU-filled buffer without a CPU instance upload.
    void dispatchParticleCompute(rhi::IRHICommandList* cmd);

    // 2D HUD / UI overlay: immediate-mode batched quads + bitmap text drawn
    // after the 3D scene with alpha blending and no depth test.
    UISystem m_ui;

    // Declarative frame-pass scheduler (task 23). render() rebuilds this each
    // frame with the shadow / scene / tonemap+HUD passes and replays it via
    // execute(); the bloom step still manages its own internal passes and runs
    // as a plain call between graph executions. Reused (cleared, not realloced)
    // across frames.
    RenderGraph m_renderGraph;

#ifdef DS_DEV
    // DS_DEV shader hot-reload (task 22). Polled once every kShaderPollFrames
    // frames in update(); on a reported change the matching shader is reloaded
    // and its pipeline reissued in place via reloadShader(). Compiled out
    // entirely in shipping builds (poll() is an inline no-op there).
    ShaderWatcher m_shaderWatcher;
    uint64_t m_frameCounter                     = 0;
    static constexpr uint64_t kShaderPollFrames = 30;
    // Reissues the pipeline(s) that consume `name`'s shaders after a recompile.
    // Currently rebuilds the mesh pipeline as the concrete proof-of-path; other
    // shaders are logged as "reload requested" (see implementation notes).
    void reloadShader(const std::string& name);
#endif

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

    // Lua scripting host: owns the wave/parry/pickups gameplay state machines
    // (and, in later steps, boss + enemy AI), data-drives enemy stat overrides,
    // and fires onWaveStart / onEnemyDeath / onPlayerDeath callbacks.
    // Initialised in initScene(); every wrapper is a no-op-friendly graceful
    // fallback if a script failed to load.
    ScriptSystem m_scripts;
    // Cached enemy stats from the loaded Lua config (or hardcoded defaults when
    // the script provided none). Applied to each spawned enemy.
    ScriptEnemyStats m_enemyStats;
    // Wires Callbacks (camera/player/time + the gameplay action hooks) and
    // loads every file in kScripts, in order. A missing/broken file just
    // leaves its module's functions undefined (every ScriptSystem wrapper
    // degrades gracefully), so load order doesn't matter — each script owns
    // an independent ds.<module> table.
    void initScripts();
    static constexpr const char* kScripts[] = {
        "scripts/enemy_ai.lua", "scripts/wave.lua",    "scripts/boss.lua",
        "scripts/parry.lua",    "scripts/pickups.lua", "scripts/hooks.lua",
    };
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
    // Spawns a visible enemy (Ranged) projectile entity from an AI request.
    void spawnEnemyProjectile(const glm::vec3& origin, const glm::vec3& velocity, int damage, uint32_t ownerBodyId);
    // Resolves enemy projectiles against the player BEFORE ProjectileSystem runs:
    // an active parry reflects an incoming bolt back at enemies (flip velocity +
    // re-own to the player, task 35), otherwise a bolt that reaches the player
    // applies damage (gated by i-frames) and is destroyed.
    void processEnemyProjectiles();
    // Enemy-death ragdoll (task 36): for each enemy whose health has dropped to
    // <=0 this frame, spawn a handful of dynamic gib boxes (with outward
    // velocity) and remove the enemy's capsule body from physics (fixes the
    // long-standing capsule leak), then let enemySystem destroy the entity.
    void handleEnemyDeaths();
    // Syncs each gib entity's Transform from its physics body, counts down its
    // despawn timer, and on expiry destroys the entity + removes its Jolt body.
    void updateGibs(float dt);

    // --- Pickups (task 33). ------------------------------------------------
    // Spawns a collectable orb (spinning box mesh) at `position`.
    void spawnPickup(const glm::vec3& position, PickupComponent::Kind kind, int value);
    // Each frame: spin pickup meshes, and for any pickup within range of the
    // player apply its effect (heal / refill ammo / dash charge), play a cue +
    // VFX, and destroy the entity (mesh auto-freed on_destroy).
    void pickupSystem(float dt);

    // --- Boss (task 40). ---------------------------------------------------
    // Spawns the single boss entity (large box, high health, BossComponent) as
    // the final wave instead of an immediate Victory.
    void spawnBoss();
    // Drives the boss phase transitions + telegraphed volley / charge attacks
    // and the parryable vulnerable window. On boss death -> Victory.
    void bossSystem(float dt);
    // True while a boss entity is alive (drives the HUD boss bar + wave flow).
    bool bossAlive() const;
    // Removes every live enemy capsule + gib body from physics and clears the
    // gib entities. Called on (re)start so wave-to-wave bodies don't accumulate
    // against Jolt's 1024-body cap.
    void clearPhysicsActors();

    // --- Weapon upgrades / alt-fire (task 37). -----------------------------
    // Granted upgrades per weapon slot (parallel to m_weapons), accumulated into
    // m_weaponMods. withMods(base, mods) yields the effective weapon used when
    // firing. An upgrade is granted at each wave intermission.
    std::vector<std::vector<WeaponUpgrade>> m_weaponUpgrades;
    std::vector<WeaponMods> m_weaponMods;
    // Effective (modded) copy of the active weapon for the current shot.
    WeaponComponent effectiveWeapon() const;
    // Alt-fire (right mouse): per-weapon-type variant. Mapped in fireWeapon.
    AltFireMode altFireFor(WeaponType type) const;
    bool m_altFirePressed = false;
    // The wave for which an upgrade has already been granted (avoid re-granting
    // every intermission frame). -1 = none granted yet.
    int m_lastUpgradeWave = 0;
    void grantWeaponUpgrade();

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

    // --- Game feel (task 25): screenshake + recoil + hitstop. --------------
    // Trauma is added on fire / player-hit and decays each frame; shakeOffset
    // perturbs camera yaw/pitch before viewProj. Recoil is an additive spring
    // kick on fire. Hitstop scales sim dt briefly on kill / explosion.
    ScreenShake m_screenShake;
    Recoil m_recoil;
    Hitstop m_hitstop;

    // --- Style meter (task 32): ULTRAKILL-style rank that rewards kills. ----
    StyleState m_style;
    StyleConfig m_styleConfig;

    // --- Combat feedback (task 29): floating damage numbers + hit marker. ---
    std::vector<DamageEvent> m_damageEvents;
    HitMarker m_hitMarker;

    // --- Parry tech (task 35): short window that negates damage, reflects a
    // projectile, and refunds a dash charge on success. State/timers now live
    // in Lua (assets/scripts/parry.lua, module ds.parry) behind m_scripts'
    // parry*() wrappers; Engine only keeps the input edge-detect + HUD flash.
    bool m_parryPressed  = false;
    float m_parryFlash   = 0.f; // HUD flash timer on a successful parry
    bool m_parryHeldPrev = false;
    // Edge-detect the weapon-switch this frame for style (WeaponSwitchKill).
    size_t m_prevWeaponIndex      = 0;
    bool m_weaponSwitchedRecently = false;
    float m_weaponSwitchTimer     = 0.f;

    // --- Settings / pause menu (task 44). ----------------------------------
    // Live in-memory copy of the persisted settings, applied to audio + camera
    // on construction and re-applied + re-saved on every widget change. Backed
    // by <userDir>/settings.cfg via UserStorage.
    GameSettings m_settings;
    // True while the settings sub-screen is shown (over the Menu / pause). The
    // mouse is in absolute mode while open so the cursor can hit the widgets;
    // it returns to relative (FPS look) mode when gameplay resumes.
    bool m_settingsOpen = false;
    // Index of the slider currently being dragged (-1 = none); set on press over
    // a slider track, cleared on mouse-up, so a held drag keeps tracking even
    // when the cursor leaves the track horizontally.
    int m_activeSlider = -1;
    // Tracks the window's relative-mouse mode so we only toggle SDL when it
    // actually needs to change (avoids fighting the OS cursor each frame).
    bool m_relativeMouse = true;
    // Applies the desired relative-mouse mode (true = FPS look, false = visible
    // cursor for menus) iff it differs from the current state.
    void setRelativeMouse(bool relative);
    static constexpr int kSettingsRowCount = 6; // master/sfx/music/ui/sens + toggles row

    // --- Game state machine + wave/score state (task 18). ------------------
    // m_wave is a read-back cache: every mutation goes through m_scripts'
    // wave*() wrappers (assets/scripts/wave.lua, module ds.wave), refreshed via
    // m_scripts.readWaveState() after each one. Config now lives entirely in
    // ds.wave.config; nothing in Engine.cpp reads a WaveConfig anymore.
    GameState m_state = GameState::Menu;
    WaveState m_wave;
    int m_highScore = 0; // best score loaded from disk, updated on death/victory

    // --- Persistent progression save (task 38). ----------------------------
    // Lifetime player progression (best wave / high score / kill+run totals /
    // best combo), loaded from <userDir>/save.dat at startup and rewritten on
    // each run end (death/victory). m_highScore is seeded from / kept in sync
    // with m_save.highScore so the menu/end-screen readouts persist across runs.
    // The legacy HighScore.dat is still loaded as a fallback so an existing
    // best score is not lost when migrating to the save blob.
    SaveData m_save;
    // Folds the just-finished run's stats into m_save and writes it to
    // <userDir>/save.dat (creating the dir lazily). Also updates m_highScore.
    void persistSave();
    // Cached albedo/sampler for materials built outside initScene (waves use
    // m_enemyAlbedo/m_enemySampler; the level rebuild on restart reuses these).
    rhi::RHITexture m_sceneAlbedo  = {};
    rhi::RHISampler m_sceneSampler = {};

    // Cached for enemy respawn after player death.
    rhi::RHITexture m_enemyAlbedo  = {};
    rhi::RHISampler m_enemySampler = {};

    // Level loading is a 3-tier fallback chain, relative to ds::paths::assets():
    // a Lua procedural level script first, then the binary .dslv, then the
    // hardcoded buildArena(). Each tier only runs if the previous one produced
    // nothing, so the engine always has playable geometry.
    static constexpr const char* kLevelGenScript = "scripts/level.lua";
    static constexpr const char* kStartupLevel   = "levels/arena.dslv";

    static constexpr glm::vec3 kPlayerSpawn{0.f, 1.7f, 0.f};
    // Resolved player spawn (task 42): seeded to kPlayerSpawn, then overridden by
    // the level's player-start spawn (flags bit0) if the loaded level has one.
    // Used for the initial placement AND respawn after death so both honor the
    // level. The airborne-style check still compares against kPlayerSpawn.y.
    glm::vec3 m_playerSpawn               = kPlayerSpawn;
    static constexpr float kRespawnDelay  = 2.f;
    static constexpr int kPlayerMaxHealth = 100;

    // World-space AABB of whichever level actually loaded (Lua-generated,
    // .dslv, or the buildArena fallback), computed once in initScene() and
    // reused every frame to size the sun's orthographic shadow frustum (see
    // render()'s sunLightSpaceMatrix call). MUST track the real level extent:
    // the multi-room Lua generator (assets/scripts/level.lua) can span tens
    // of units in X, and a fragment outside this box gets treated as
    // unconditionally lit by sampleSunShadow's out-of-frustum fallback — a
    // stale/undersized box here is exactly what reads as "fullbright" (every
    // surface beyond the box gets full unshadowed sun regardless of walls or
    // ceiling). Defaults to buildArena's own known extents so the third-tier
    // fallback is correct even if initScene's assignment is ever skipped.
    ds::Bounds m_levelBounds{glm::vec3{-10.2f, -0.2f, -10.2f}, glm::vec3{10.2f, 5.2f, 10.2f}};

    // Audio asset paths, relative to ds::paths::assets(). Missing files are
    // logged and skipped by AudioSystem (see assets/sfx/README.md).
    static constexpr const char* kSfxWeaponFire = "sfx/weapon_fire.wav";
    static constexpr const char* kSfxEnemyHit   = "sfx/enemy_hit.wav";
    static constexpr const char* kSfxEnemyDeath = "sfx/enemy_death.wav";
    static constexpr const char* kSfxPlayerHit  = "sfx/player_hit.wav";
    static constexpr const char* kSfxDash       = "sfx/dash.wav";
    static constexpr const char* kSfxSlide      = "sfx/slide.wav";
    static constexpr const char* kMusicTrack    = "music/theme.mp3";
    // New-mechanic feedback cues (task 44). Missing files are safe no-ops.
    static constexpr const char* kSfxExplosion = "sfx/explosion.wav";
    static constexpr const char* kSfxParry     = "sfx/parry.wav";
    static constexpr const char* kSfxPickup    = "sfx/pickup.wav";
    static constexpr const char* kSfxRankUp    = "sfx/rank_up.wav";
    static constexpr const char* kSfxFootstep  = "sfx/footstep.wav";
    static constexpr const char* kSfxUiClick   = "sfx/ui_click.wav";
    // Footstep cadence (seconds between steps while grounded + moving).
    static constexpr float kFootstepInterval = 0.38f;
    float m_footstepTimer                    = 0.f;
    // Previous-frame style rank, to detect a rank-up for the sting.
    StyleRank m_prevRank = StyleRank::D;

    Camera m_camera;
    int m_windowWidth  = 1280;
    int m_windowHeight = 720;

    uint64_t m_lastTick  = 0;
    float m_dt           = 0.f;
    float m_smoothFps    = 0.f; // EMA-smoothed FPS for the HUD counter
};

} // namespace ds
