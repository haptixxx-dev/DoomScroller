#include "engine/Engine.h"

#include "engine/HighScore.h"
#include "engine/LevelLoader.h"
#include "engine/Paths.h"
#include "engine/PlayerController.h"
#include "engine/Profiler.h"
#include "engine/ShaderLoader.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/EnemySystem.h"
#include "engine/ecs/ProjectileSystem.h"

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <string>

#include "rhi/sdl3/SDL3Device.h"

namespace ds {

static MeshComponent makeBoxMesh(rhi::IRHIDevice& device, float hw, float hh, float hd, glm::vec3 color) {
    struct BoxVert {
        glm::vec3 pos;
        glm::vec3 col;
        glm::vec2 uv;
        glm::vec3 normal;
    };

    const BoxVert verts[24] = {
        // +Y top
        {{-hw, hh, -hd}, color, {0, 0}, {0, 1, 0}},
        {{hw, hh, -hd}, color, {1, 0}, {0, 1, 0}},
        {{hw, hh, hd}, color, {1, 1}, {0, 1, 0}},
        {{-hw, hh, hd}, color, {0, 1}, {0, 1, 0}},
        // -Y bottom
        {{-hw, -hh, hd}, color, {0, 0}, {0, -1, 0}},
        {{hw, -hh, hd}, color, {1, 0}, {0, -1, 0}},
        {{hw, -hh, -hd}, color, {1, 1}, {0, -1, 0}},
        {{-hw, -hh, -hd}, color, {0, 1}, {0, -1, 0}},
        // +Z front
        {{-hw, -hh, hd}, color, {0, 0}, {0, 0, 1}},
        {{hw, -hh, hd}, color, {1, 0}, {0, 0, 1}},
        {{hw, hh, hd}, color, {1, 1}, {0, 0, 1}},
        {{-hw, hh, hd}, color, {0, 1}, {0, 0, 1}},
        // -Z back
        {{hw, -hh, -hd}, color, {0, 0}, {0, 0, -1}},
        {{-hw, -hh, -hd}, color, {1, 0}, {0, 0, -1}},
        {{-hw, hh, -hd}, color, {1, 1}, {0, 0, -1}},
        {{hw, hh, -hd}, color, {0, 1}, {0, 0, -1}},
        // +X right
        {{hw, -hh, hd}, color, {0, 0}, {1, 0, 0}},
        {{hw, -hh, -hd}, color, {1, 0}, {1, 0, 0}},
        {{hw, hh, -hd}, color, {1, 1}, {1, 0, 0}},
        {{hw, hh, hd}, color, {0, 1}, {1, 0, 0}},
        // -X left
        {{-hw, -hh, -hd}, color, {0, 0}, {-1, 0, 0}},
        {{-hw, -hh, hd}, color, {1, 0}, {-1, 0, 0}},
        {{-hw, hh, hd}, color, {1, 1}, {-1, 0, 0}},
        {{-hw, hh, -hd}, color, {0, 1}, {-1, 0, 0}},
    };
    const uint16_t idx[36] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                              12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

    rhi::BufferDesc vbd{};
    vbd.size  = sizeof(verts);
    vbd.usage = rhi::BufferUsage::Vertex;
    auto vb   = device.createBuffer(vbd);
    device.uploadImmediate(vb, verts, vbd.size);

    rhi::BufferDesc ibd{};
    ibd.size  = sizeof(idx);
    ibd.usage = rhi::BufferUsage::Index;
    auto ib   = device.createBuffer(ibd);
    device.uploadImmediate(ib, idx, ibd.size);

    return MeshComponent{vb, ib, 36u, rhi::IndexType::Uint16};
}

// 4x4 RGBA8 checkerboard (white/grey) used as a placeholder texture
static uint8_t makeCheckerboard(uint8_t* out, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            bool bright = ((x + y) & 1) == 0;
            uint8_t v   = bright ? 255u : 128u;
            uint8_t* p  = out + (y * w + x) * 4;
            p[0] = p[1] = p[2] = v;
            p[3]               = 255u;
        }
    }
    return 0;
}

Engine::Engine(const EngineConfig& cfg) : m_windowWidth(cfg.width), m_windowHeight(cfg.height) {
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());

    m_window = SDL_CreateWindow(cfg.title.data(), cfg.width, cfg.height, 0);
    if (!m_window)
        throw std::runtime_error(SDL_GetError());

    SDL_SetWindowRelativeMouseMode(m_window, true);

    m_device   = rhi::createDevice(m_window);
    m_textures = std::make_unique<TextureManager>(*m_device);
    m_running  = true;

    m_physics.init();

    // Audio is non-fatal: if the device fails to open, AudioSystem becomes a
    // silent no-op and the rest of the engine is unaffected.
    m_audio.init();
    m_audio.playMusic(kMusicTrack);

    initScene();

    // Lua data-driven tuning (waves + enemy stats). Must run after m_waveConfig
    // holds its hardcoded defaults so the script only overrides what it sets.
    initScripts();

    // Persistent high score; shown on the menu / end screens.
    m_highScore = highscore::load(highscore::defaultPath());

    // Start at the menu; a click / Enter begins the first run.
    m_state = GameState::Menu;

    m_lastTick = SDL_GetPerformanceCounter();
}

Engine::~Engine() {
    m_world.clear();
    m_textures->destroyAll();
    if (m_linearSampler.valid())
        m_device->destroySampler(m_linearSampler);
    if (m_meshPipeline.valid())
        m_device->destroyPipeline(m_meshPipeline);
    if (m_meshVS.valid())
        m_device->destroyShader(m_meshVS);
    if (m_meshFS.valid())
        m_device->destroyShader(m_meshFS);
    if (m_particleAlphaPipe.valid())
        m_device->destroyPipeline(m_particleAlphaPipe);
    if (m_particleAdditivePipe.valid())
        m_device->destroyPipeline(m_particleAdditivePipe);
    if (m_particleVS.valid())
        m_device->destroyShader(m_particleVS);
    if (m_particleFS.valid())
        m_device->destroyShader(m_particleFS);
    if (m_particleInstanceBuffer.valid())
        m_device->destroyBuffer(m_particleInstanceBuffer);
    m_ui.shutdown(*m_device);
    if (m_depthTexture.valid())
        m_device->destroyTexture(m_depthTexture);
    m_textures.reset();
    m_device.reset();
    if (m_window)
        SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void Engine::initScene() {
    ShaderLoader loader(static_cast<SDL_GPUDevice*>(m_device->nativeDevice()), paths::shaders());
    m_meshVS = loader.load(*m_device, "mesh", rhi::ShaderStage::Vertex, 0, 1);
    m_meshFS = loader.load(*m_device, "mesh", rhi::ShaderStage::Fragment, 1, 1);

    // Sampler
    rhi::SamplerDesc samplerDesc{};
    m_linearSampler = m_device->createSampler(samplerDesc);

    // Depth texture
    rhi::TextureDesc depthDesc{};
    depthDesc.width          = static_cast<uint32_t>(m_windowWidth);
    depthDesc.height         = static_cast<uint32_t>(m_windowHeight);
    depthDesc.format         = rhi::TextureFormat::D32Float;
    depthDesc.isDepthStencil = true;
    depthDesc.debugName      = "depth";
    m_depthTexture           = m_device->createTexture(depthDesc);

    // Vertex layout: interleaved pos + color + uv + normal
    rhi::VertexAttribute attrs[4];
    attrs[0].location     = 0;
    attrs[0].binding      = 0;
    attrs[0].offset       = offsetof(Vertex, pos);
    attrs[0].elementCount = 3;
    attrs[0].isFloat      = true;
    attrs[1].location     = 1;
    attrs[1].binding      = 0;
    attrs[1].offset       = offsetof(Vertex, color);
    attrs[1].elementCount = 3;
    attrs[1].isFloat      = true;
    attrs[2].location     = 2;
    attrs[2].binding      = 0;
    attrs[2].offset       = offsetof(Vertex, uv);
    attrs[2].elementCount = 2;
    attrs[2].isFloat      = true;
    attrs[3].location     = 3;
    attrs[3].binding      = 0;
    attrs[3].offset       = offsetof(Vertex, normal);
    attrs[3].elementCount = 3;
    attrs[3].isFloat      = true;

    rhi::VertexBinding vbinding{};
    vbinding.binding   = 0;
    vbinding.stride    = static_cast<uint32_t>(sizeof(Vertex));
    vbinding.instanced = false;

    rhi::ColorTargetDesc colorTarget{};
    colorTarget.format = m_device->swapchainFormat();

    rhi::PipelineDesc pipeDesc{};
    pipeDesc.vertexShader     = m_meshVS;
    pipeDesc.fragmentShader   = m_meshFS;
    pipeDesc.vertexAttributes = {attrs, 4};
    pipeDesc.vertexBindings   = {&vbinding, 1};
    pipeDesc.colorTargets     = {&colorTarget, 1};
    pipeDesc.hasDepth         = true;
    pipeDesc.depthTest        = true;
    pipeDesc.depthWrite       = true;
    pipeDesc.depthFormat      = rhi::TextureFormat::D32Float;
    pipeDesc.depthCompare     = rhi::CompareOp::Less;
    pipeDesc.cullMode         = rhi::CullMode::None;
    m_meshPipeline            = m_device->createPipeline(pipeDesc);

    // Checkerboard placeholder texture
    constexpr uint32_t kTexW = 8, kTexH = 8;
    uint8_t pixels[kTexW * kTexH * 4];
    makeCheckerboard(pixels, kTexW, kTexH);
    rhi::RHITexture albedo = m_textures->createFromMemory(pixels, kTexW, kTexH, "checkerboard");

    // Camera starts inside the arena at eye height
    m_camera.position = kPlayerSpawn;
    m_camera.yaw      = -90.f;

    // Prefer a data-driven level file; fall back to the hardcoded arena when it
    // is missing so the engine always has playable geometry.
    if (!loadLevel(kStartupLevel, albedo, m_linearSampler))
        buildArena(albedo, m_linearSampler);

    // Player capsule: radius 0.4, halfHeight 0.5 (total height ~1.8 units)
    m_playerBodyId = m_physics.addCapsule(0.5f, 0.4f, kPlayerSpawn);
    m_player       = std::make_unique<PlayerController>(m_physics, m_playerBodyId);

    // Player entity holds gameplay state (health). The capsule lives in physics.
    m_playerEntity = m_world.create();
    m_world.emplace<HealthComponent>(m_playerEntity, HealthComponent{kPlayerMaxHealth, kPlayerMaxHealth});

    // Enemy material is reused when respawning the wave.
    m_enemyAlbedo  = albedo;
    m_enemySampler = m_linearSampler;
    m_sceneAlbedo  = albedo;
    m_sceneSampler = m_linearSampler;

    // Projectile meshes reuse the placeholder albedo/sampler.
    m_projectileAlbedo  = albedo;
    m_projectileSampler = m_linearSampler;

    // Weapon loadout (slot order = number keys 1..N). Slot 0 is the original
    // hitscan rifle; slots 1/2 are projectile weapons.
    m_weapons.clear();
    {
        WeaponComponent hitscan{};
        hitscan.type     = WeaponType::Hitscan;
        hitscan.damage   = 25;
        hitscan.fireRate = 5.f;
        m_weapons.push_back(hitscan);

        WeaponComponent rocket{};
        rocket.type               = WeaponType::Rocket;
        rocket.damage             = 80;
        rocket.fireRate           = 1.f;
        rocket.projectileSpeed    = 22.f;
        rocket.projectileLifetime = 5.f;
        rocket.splashRadius       = 4.f;
        rocket.muzzleOffset       = glm::vec3{0.f, 0.f, 0.6f};
        m_weapons.push_back(rocket);

        WeaponComponent plasma{};
        plasma.type               = WeaponType::Plasma;
        plasma.damage             = 18;
        plasma.fireRate           = 8.f;
        plasma.projectileSpeed    = 55.f;
        plasma.projectileLifetime = 3.f;
        plasma.splashRadius       = 0.f;
        plasma.muzzleOffset       = glm::vec3{0.f, 0.f, 0.4f};
        m_weapons.push_back(plasma);
    }
    m_weaponIndex = 0;

    initParticles();

    m_ui.init(*m_device, m_device->nativeDevice(), paths::shaders());
}

void Engine::initParticles() {
    ShaderLoader loader(static_cast<SDL_GPUDevice*>(m_device->nativeDevice()), paths::shaders());
    m_particleVS = loader.load(*m_device, "particle", rhi::ShaderStage::Vertex, 0, 1);
    m_particleFS = loader.load(*m_device, "particle", rhi::ShaderStage::Fragment, 0, 0);

    // Per-instance vertex layout: vec3 pos, float size, vec4 color.
    // Matches ParticleSystem::Instance.
    static rhi::VertexAttribute pattrs[3];
    pattrs[0].location     = 0;
    pattrs[0].binding      = 0;
    pattrs[0].offset       = offsetof(ParticleSystem::Instance, position);
    pattrs[0].elementCount = 3;
    pattrs[0].isFloat      = true;
    pattrs[1].location     = 1;
    pattrs[1].binding      = 0;
    pattrs[1].offset       = offsetof(ParticleSystem::Instance, size);
    pattrs[1].elementCount = 1;
    pattrs[1].isFloat      = true;
    pattrs[2].location     = 2;
    pattrs[2].binding      = 0;
    pattrs[2].offset       = offsetof(ParticleSystem::Instance, color);
    pattrs[2].elementCount = 4;
    pattrs[2].isFloat      = true;

    static rhi::VertexBinding pbinding{};
    pbinding.binding   = 0;
    pbinding.stride    = static_cast<uint32_t>(sizeof(ParticleSystem::Instance));
    pbinding.instanced = true;

    // Shaders output premultiplied-alpha color, so both blend modes use a
    // src factor of One: additive adds straight on top; alpha uses
    // One * src + (1-srcA) * dst for correct over-compositing.
    rhi::ColorTargetDesc alphaTarget{};
    alphaTarget.format             = m_device->swapchainFormat();
    alphaTarget.blend.blendEnabled = true;
    alphaTarget.blend.srcColor     = rhi::BlendFactor::One;
    alphaTarget.blend.dstColor     = rhi::BlendFactor::OneMinusSrcAlpha;
    alphaTarget.blend.colorOp      = rhi::BlendOp::Add;
    alphaTarget.blend.srcAlpha     = rhi::BlendFactor::One;
    alphaTarget.blend.dstAlpha     = rhi::BlendFactor::OneMinusSrcAlpha;
    alphaTarget.blend.alphaOp      = rhi::BlendOp::Add;

    rhi::ColorTargetDesc additiveTarget{};
    additiveTarget.format             = m_device->swapchainFormat();
    additiveTarget.blend.blendEnabled = true;
    additiveTarget.blend.srcColor     = rhi::BlendFactor::One;
    additiveTarget.blend.dstColor     = rhi::BlendFactor::One;
    additiveTarget.blend.colorOp      = rhi::BlendOp::Add;
    additiveTarget.blend.srcAlpha     = rhi::BlendFactor::One;
    additiveTarget.blend.dstAlpha     = rhi::BlendFactor::One;
    additiveTarget.blend.alphaOp      = rhi::BlendOp::Add;

    rhi::PipelineDesc pipeDesc{};
    pipeDesc.vertexShader     = m_particleVS;
    pipeDesc.fragmentShader   = m_particleFS;
    pipeDesc.vertexAttributes = {pattrs, 3};
    pipeDesc.vertexBindings   = {&pbinding, 1};
    pipeDesc.hasDepth         = true;
    pipeDesc.depthFormat      = rhi::TextureFormat::D32Float;
    pipeDesc.depthTest        = true;
    pipeDesc.depthWrite       = false; // particles don't occlude each other
    pipeDesc.depthCompare     = rhi::CompareOp::Less;
    pipeDesc.cullMode         = rhi::CullMode::None;

    rhi::ColorTargetDesc alphaTargets[1] = {alphaTarget};
    pipeDesc.colorTargets                = {alphaTargets, 1};
    m_particleAlphaPipe                  = m_device->createPipeline(pipeDesc);

    rhi::ColorTargetDesc additiveTargets[1] = {additiveTarget};
    pipeDesc.colorTargets                   = {additiveTargets, 1};
    m_particleAdditivePipe                  = m_device->createPipeline(pipeDesc);

    // Dynamic per-frame instance buffer, sized for the full pool.
    rhi::BufferDesc instDesc{};
    instDesc.size            = sizeof(ParticleSystem::Instance) * kMaxParticlesGPU;
    instDesc.usage           = rhi::BufferUsage::Vertex;
    instDesc.debugName       = "particle_instances";
    m_particleInstanceBuffer = m_device->createBuffer(instDesc);
}

void Engine::initScripts() {
    ScriptSystem::Callbacks cb{};

    // ds.spawn_enemy(x, y, z [, type]) -> entity id. Reuses the enemy material
    // cached during initScene; returns the entt entity id as a plain integer.
    cb.spawnEnemy = [this](float x, float y, float z, int /*type*/) -> uint32_t {
        entt::entity e = spawnEnemy({x, y, z}, m_enemyAlbedo, m_enemySampler);
        return static_cast<uint32_t>(entt::to_integral(e));
    };

    // ds.get_field(entity, "health"|"speed"|"damage") -> number.
    cb.getEntityField = [this](uint32_t entity, std::string_view field) -> float {
        auto e = static_cast<entt::entity>(entity);
        if (!m_world.valid(e))
            return 0.f;
        if (auto* enemy = m_world.try_get<EnemyComponent>(e)) {
            if (field == "health")
                return static_cast<float>(enemy->health);
            if (field == "speed")
                return enemy->moveSpeed;
            if (field == "damage")
                return static_cast<float>(enemy->attackDamage);
        }
        if (auto* hp = m_world.try_get<HealthComponent>(e)) {
            if (field == "health")
                return static_cast<float>(hp->current);
        }
        return 0.f;
    };

    // ds.set_field(entity, "health"|"speed"|"damage", value).
    cb.setEntityField = [this](uint32_t entity, std::string_view field, float value) {
        auto e = static_cast<entt::entity>(entity);
        if (!m_world.valid(e))
            return;
        if (auto* enemy = m_world.try_get<EnemyComponent>(e)) {
            if (field == "health")
                enemy->health = static_cast<int>(value);
            else if (field == "speed")
                enemy->moveSpeed = value;
            else if (field == "damage")
                enemy->attackDamage = static_cast<int>(value);
        }
        if (auto* hp = m_world.try_get<HealthComponent>(e)) {
            if (field == "health")
                hp->current = static_cast<int>(value);
        }
    };

    // ds.emit_event(name [, number]) -> logged for now; a hook point for future
    // game-side event routing (sound cues, achievements, etc.).
    cb.emitEvent = [](std::string_view name, double value) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[script event] %.*s = %g", static_cast<int>(name.size()),
                    name.data(), value);
    };

    if (!m_scripts.init(cb))
        return;

    // Load the wave/enemy config; missing or broken -> keep hardcoded defaults.
    std::filesystem::path scriptPath = paths::assets() / kWaveScript;
    if (m_scripts.loadFile(scriptPath.string())) {
        ScriptWaveConfig wc = m_scripts.waveConfig();
        if (wc.overrode) {
            m_waveConfig.baseEnemies       = wc.baseEnemies;
            m_waveConfig.enemiesPerWave    = wc.enemiesPerWave;
            m_waveConfig.maxEnemiesPerWave = wc.maxEnemiesPerWave;
            m_waveConfig.maxWaves          = wc.maxWaves;
            m_waveConfig.intermissionTime  = wc.intermissionTime;
            m_waveConfig.killScore         = wc.killScore;
        }
        m_enemyStats = m_scripts.enemyStats();
    }
}

void Engine::spawnEnemies() {
    // Spawn enemies at corners of the arena.
    spawnEnemy({-7.f, 1.5f, -7.f}, m_enemyAlbedo, m_enemySampler);
    spawnEnemy({7.f, 1.5f, -7.f}, m_enemyAlbedo, m_enemySampler);
    spawnEnemy({0.f, 1.5f, 7.f}, m_enemyAlbedo, m_enemySampler);
}

std::vector<glm::vec3> Engine::waveSpawnPositions() const {
    std::vector<glm::vec3> out;
    auto view = m_world.view<const SpawnPoint>();
    for (auto [entity, sp] : view.each())
        out.push_back(sp.position);

    // No SpawnPoint entities: fall back to the four arena corners (matches the
    // hardcoded buildArena footprint, kept a little inside the walls).
    if (out.empty()) {
        out = {
            {-7.f, 1.5f, -7.f},
            {7.f, 1.5f, -7.f},
            {-7.f, 1.5f, 7.f},
            {7.f, 1.5f, 7.f},
        };
    }
    return out;
}

void Engine::spawnWaveEnemies(int count) {
    std::vector<glm::vec3> spots = waveSpawnPositions();
    if (spots.empty())
        return;
    // Round-robin across the available spawn points so larger waves still fan
    // out across the arena instead of stacking on one corner.
    for (int i = 0; i < count; ++i)
        spawnEnemy(spots[i % spots.size()], m_enemyAlbedo, m_enemySampler);
}

void Engine::respawnPlayer() {
    auto& health   = m_world.get<HealthComponent>(m_playerEntity);
    health.current = health.max;

    m_physics.setPosition(m_playerBodyId, kPlayerSpawn);
    m_physics.setLinearVelocity(m_playerBodyId, {0.f, 0.f, 0.f});
    m_camera.position = kPlayerSpawn;

    m_playerDead    = false;
    m_playerIFrames = 0.f;
}

void Engine::startGame() {
    // Clear any leftover gameplay entities (enemies + in-flight projectiles)
    // from a previous run; the arena geometry, player and lights persist.
    {
        auto enemies = m_world.view<EnemyComponent>();
        m_world.destroy(enemies.begin(), enemies.end());
        auto projectiles = m_world.view<ProjectileComponent>();
        m_world.destroy(projectiles.begin(), projectiles.end());
    }

    respawnPlayer();

    resetWave(m_wave);
    m_weaponIndex = 0;
    m_damageFlash = 0.f;

    // Kick off the first wave immediately (no intermission before wave 1).
    advanceWave(m_wave, m_waveConfig);
    if (m_wave.spawnPending) {
        int n = enemiesForWave(m_wave.wave, m_waveConfig);
        spawnWaveEnemies(n);
        m_wave.aliveEnemies = n;
        m_wave.spawnPending = false;
        m_scripts.onWaveStart(m_wave.wave);
    }

    m_state = GameState::Playing;
}

void Engine::recordHighScore() {
    if (highscore::save(highscore::defaultPath(), m_wave.score))
        m_highScore = m_wave.score;
    else
        m_highScore = std::max(m_highScore, m_wave.score);
}

void Engine::fireWeapon() {
    const WeaponComponent& weapon = currentWeapon();
    glm::vec3 origin              = m_camera.position;
    glm::vec3 dir                 = glm::normalize(m_camera.front());

    // Projectile weapons spawn a flying entity; the rest of the hit/damage
    // resolution happens in projectileSystem during update().
    if (weapon.type != WeaponType::Hitscan) {
        spawnProjectile(weapon, origin, dir);
        return;
    }

    glm::vec3 hitPoint{0.f};
    uint32_t hitId = m_physics.castRay(origin, dir, 100.f, m_playerBodyId, hitPoint);
    if (hitId == UINT32_MAX)
        return;

    auto view = m_world.view<EnemyComponent, Transform>();
    for (auto [entity, enemy, transform] : view.each()) {
        if (enemy.physicsBodyId == hitId) {
            enemy.health -= weapon.damage;
            // Death is handled by enemySystem next frame; play the death cue now
            // (positioned at the enemy) and otherwise an impact cue.
            if (enemy.health <= 0)
                m_audio.playAt(kSfxEnemyDeath, transform.position);
            else
                m_audio.playAt(kSfxEnemyHit, transform.position);
            // Blood sprays back toward the shooter.
            m_particles.emit(ParticleSystem::Effect::BloodBurst, hitPoint, -dir, 24);
            return;
        }
    }

    // Hit geometry (wall/floor/ceiling): kick sparks off the surface back
    // toward the shooter.
    m_particles.emit(ParticleSystem::Effect::ImpactSparks, hitPoint, -dir, 16);
}

void Engine::spawnProjectile(const WeaponComponent& weapon, const glm::vec3& origin, const glm::vec3& dir) {
    // Offset the spawn forward so the bolt leaves the muzzle, not the eye.
    glm::vec3 spawn = origin + dir * (weapon.muzzleOffset.z + 0.5f);

    // Plasma bolts are small/green and additive-bright; rockets are larger/orange.
    bool plasma     = weapon.type == WeaponType::Plasma;
    float half      = plasma ? 0.08f : 0.15f;
    glm::vec3 color = plasma ? glm::vec3{0.3f, 1.f, 0.5f} : glm::vec3{1.f, 0.5f, 0.15f};

    auto e = m_world.create();
    Transform t{};
    t.position = spawn;
    m_world.emplace<Transform>(e, t);
    m_world.emplace<MeshComponent>(e, makeBoxMesh(*m_device, half, half, half, color));
    m_world.emplace<MaterialComponent>(e, MaterialComponent{m_projectileAlbedo, m_projectileSampler});

    ProjectileComponent pc{};
    pc.velocity     = dir * weapon.projectileSpeed;
    pc.damage       = weapon.damage;
    pc.lifetime     = weapon.projectileLifetime;
    pc.splashRadius = weapon.splashRadius;
    pc.ownerBodyId  = m_playerBodyId;
    m_world.emplace<ProjectileComponent>(e, pc);

    // A faint glow that travels with the bolt (one-frame; refreshed each frame
    // is overkill, a brief light at spawn is enough to pop nearby geometry).
    spawnTransientLight(spawn, color, plasma ? 3.f : 5.f, 2.5f, 0.1f);
}

entt::entity Engine::spawnEnemy(glm::vec3 position, rhi::RHITexture albedo, rhi::RHISampler sampler) {
    constexpr float kHalfH = 0.6f, kRadius = 0.3f;
    uint32_t bodyId = m_physics.addCapsule(kHalfH, kRadius, position);

    auto e = m_world.create();
    Transform t{};
    t.position = position;
    m_world.emplace<Transform>(e, t);
    m_world.emplace<MeshComponent>(e, makeBoxMesh(*m_device, 0.3f, kHalfH + kRadius, 0.3f, {1.f, 0.2f, 0.2f}));
    m_world.emplace<MaterialComponent>(e, MaterialComponent{albedo, sampler});
    EnemyComponent ec{};
    ec.physicsBodyId = bodyId;
    // Data-driven enemy tuning from the Lua config (falls back to component
    // defaults when the script provided none).
    if (m_enemyStats.overrode) {
        ec.health       = m_enemyStats.health;
        ec.moveSpeed    = m_enemyStats.speed;
        ec.attackDamage = m_enemyStats.damage;
    }
    m_world.emplace<EnemyComponent>(e, ec);
    return e;
}

void Engine::buildArena(rhi::RHITexture albedo, rhi::RHISampler sampler) {
    // 20x5x20 box room. CullMode::None so all faces render from inside.
    // UV coords tile the checkerboard: 1 tile per world unit.

    struct Face {
        Vertex verts[4];
        const char* name;
    };

    // Each face: 4 verts, normals face room interior (inward).
    // Indices always 0,1,2,0,2,3. CullMode::None so winding doesn't matter.
    const float W = 10.f, H = 5.f, D = 10.f, T = 0.1f; // half-extents X,Y,Z; T = slab thickness
    const Face faces[] = {
        // Floor (y=0), normal +Y
        {{{{-W, 0, -D}, {1, 1, 1}, {0, D}, {0, 1, 0}},
          {{W, 0, -D}, {1, 1, 1}, {W * 2, D}, {0, 1, 0}},
          {{W, 0, D}, {1, 1, 1}, {W * 2, 0}, {0, 1, 0}},
          {{-W, 0, D}, {1, 1, 1}, {0, 0}, {0, 1, 0}}},
         "floor"},
        // Ceiling (y=H), normal -Y
        {{{{-W, H, D}, {1, 1, 1}, {0, D}, {0, -1, 0}},
          {{W, H, D}, {1, 1, 1}, {W * 2, D}, {0, -1, 0}},
          {{W, H, -D}, {1, 1, 1}, {W * 2, 0}, {0, -1, 0}},
          {{-W, H, -D}, {1, 1, 1}, {0, 0}, {0, -1, 0}}},
         "ceil"},
        // North wall (z=-D), normal +Z (inward)
        {{{{-W, H, -D}, {1, 1, 1}, {0, H}, {0, 0, 1}},
          {{W, H, -D}, {1, 1, 1}, {W * 2, H}, {0, 0, 1}},
          {{W, 0, -D}, {1, 1, 1}, {W * 2, 0}, {0, 0, 1}},
          {{-W, 0, -D}, {1, 1, 1}, {0, 0}, {0, 0, 1}}},
         "wall_n"},
        // South wall (z=+D), normal -Z (inward)
        {{{{W, H, D}, {1, 1, 1}, {0, H}, {0, 0, -1}},
          {{-W, H, D}, {1, 1, 1}, {W * 2, H}, {0, 0, -1}},
          {{-W, 0, D}, {1, 1, 1}, {W * 2, 0}, {0, 0, -1}},
          {{W, 0, D}, {1, 1, 1}, {0, 0}, {0, 0, -1}}},
         "wall_s"},
        // West wall (x=-W), normal +X (inward)
        {{{{-W, H, -D}, {1, 1, 1}, {0, H}, {1, 0, 0}},
          {{-W, H, D}, {1, 1, 1}, {D * 2, H}, {1, 0, 0}},
          {{-W, 0, D}, {1, 1, 1}, {D * 2, 0}, {1, 0, 0}},
          {{-W, 0, -D}, {1, 1, 1}, {0, 0}, {1, 0, 0}}},
         "wall_w"},
        // East wall (x=+W), normal -X (inward)
        {{{{W, H, D}, {1, 1, 1}, {0, H}, {-1, 0, 0}},
          {{W, H, -D}, {1, 1, 1}, {D * 2, H}, {-1, 0, 0}},
          {{W, 0, -D}, {1, 1, 1}, {D * 2, 0}, {-1, 0, 0}},
          {{W, 0, D}, {1, 1, 1}, {0, 0}, {-1, 0, 0}}},
         "wall_e"},
    };

    static const uint16_t kQuadIdx[] = {0, 1, 2, 0, 2, 3};

    for (const auto& face : faces) {
        rhi::BufferDesc vbd{};
        vbd.size      = sizeof(face.verts);
        vbd.usage     = rhi::BufferUsage::Vertex;
        vbd.debugName = face.name;
        auto vb       = m_device->createBuffer(vbd);
        m_device->uploadImmediate(vb, face.verts, vbd.size);

        rhi::BufferDesc ibd{};
        ibd.size  = sizeof(kQuadIdx);
        ibd.usage = rhi::BufferUsage::Index;
        auto ib   = m_device->createBuffer(ibd);
        m_device->uploadImmediate(ib, kQuadIdx, ibd.size);

        auto e = m_world.create();
        m_world.emplace<Transform>(e);
        m_world.emplace<MeshComponent>(e, MeshComponent{vb, ib, 6u, rhi::IndexType::Uint16});
        m_world.emplace<MaterialComponent>(e, MaterialComponent{albedo, sampler});
    }

    m_physics.addStaticBox({0.f, -T, 0.f}, {W, T, D});           // floor
    m_physics.addStaticBox({0.f, H + T, 0.f}, {W, T, D});        // ceiling
    m_physics.addStaticBox({0.f, H / 2, -D - T}, {W, H / 2, T}); // north wall
    m_physics.addStaticBox({0.f, H / 2, D + T}, {W, H / 2, T});  // south wall
    m_physics.addStaticBox({-W - T, H / 2, 0.f}, {T, H / 2, D}); // west wall
    m_physics.addStaticBox({W + T, H / 2, 0.f}, {T, H / 2, D});  // east wall
}

bool Engine::loadLevel(const char* relPath, rhi::RHITexture albedo, rhi::RHISampler sampler) {
    std::filesystem::path full = paths::assets() / relPath;
    return LevelLoader::load(full, m_world, m_physics, *m_device, albedo, sampler);
}

void Engine::run() {
    while (m_running) {
        processEvents();
        update();
        render();
        DS_FRAME_MARK();
    }
}

void Engine::processEvents() {
    DS_ZONE();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            m_running = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_ESCAPE) {
                // In Playing: ESC pauses back to the Menu. Anywhere else: quit.
                if (m_state == GameState::Playing)
                    m_state = GameState::Menu;
                else
                    m_running = false;
            }
            // Enter (re)starts a run from any non-playing screen.
            if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && m_state != GameState::Playing)
                startGame();
            // Number keys 1..N select a weapon slot directly.
            if (e.key.key >= SDLK_1 && e.key.key <= SDLK_9) {
                size_t slot = static_cast<size_t>(e.key.key - SDLK_1);
                if (slot < m_weapons.size())
                    m_weaponIndex = slot;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            m_camera.rotate(e.motion.xrel, e.motion.yrel);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                // Click starts / restarts from a non-playing screen; otherwise
                // it fires the active weapon.
                if (m_state != GameState::Playing)
                    startGame();
                else
                    m_firePressed = true;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            // Wheel cycles weapons (wrapping). Ignore when no loadout exists.
            if (!m_weapons.empty() && e.wheel.y != 0.f) {
                int n         = static_cast<int>(m_weapons.size());
                int delta     = e.wheel.y > 0.f ? 1 : -1;
                m_weaponIndex = static_cast<size_t>((static_cast<int>(m_weaponIndex) + delta + n) % n);
            }
            break;
        default:
            break;
        }
    }
}

void Engine::update() {
    DS_ZONE();

    uint64_t now = SDL_GetPerformanceCounter();
    m_dt         = static_cast<float>(now - m_lastTick) / static_cast<float>(SDL_GetPerformanceFrequency());
    m_dt         = std::min(m_dt, 0.05f);
    m_lastTick   = now;

    m_timeAccum += m_dt;
    if (m_damageFlash > 0.f)
        m_damageFlash -= m_dt;

    // Only the Playing state runs the full simulation; Menu / Dead / Victory
    // are paused and just show their overlay (handled in render()).
    if (m_state != GameState::Playing)
        return;
    updatePlaying();
}

void Engine::updatePlaying() {
    DS_ZONE();

    const bool* keys = SDL_GetKeyboardState(nullptr);

    // Project camera front/right onto XZ plane for ground-relative movement
    glm::vec3 camFront = m_camera.front();
    glm::vec3 fwd      = glm::normalize(glm::vec3{camFront.x, 0.f, camFront.z});
    glm::vec3 rgt      = m_camera.right();

    glm::vec3 moveDir{0.f};
    if (keys[SDL_SCANCODE_W])
        moveDir += fwd;
    if (keys[SDL_SCANCODE_S])
        moveDir -= fwd;
    if (keys[SDL_SCANCODE_D])
        moveDir += rgt;
    if (keys[SDL_SCANCODE_A])
        moveDir -= rgt;
    if (glm::length(moveDir) > 0.f)
        moveDir = glm::normalize(moveDir);

    bool jump = keys[SDL_SCANCODE_SPACE];

    // Movement tech input: Shift = dash (edge-triggered), Ctrl = slide/crouch.
    bool dashHeld    = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    bool dashPressed = dashHeld && !m_dashHeldPrev;
    m_dashHeldPrev   = dashHeld;
    bool crouchHeld  = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];

    m_physics.step(m_dt);
    m_player->update(m_camera, moveDir, jump, dashPressed, crouchHeld, m_dt);

    if (m_playerIFrames > 0.f)
        m_playerIFrames -= m_dt;
    // Fold dash invulnerability into the shared damage gate (task 11 i-frames).
    if (m_player->iFrames() > m_playerIFrames)
        m_playerIFrames = m_player->iFrames();

    // Dash / slide SFX + VFX hooks (audio + particles always exist on Engine).
    if (m_player->dashedThisFrame()) {
        m_audio.play(kSfxDash);
        // Trail puff behind the dash, biased back toward the player.
        glm::vec3 back = -glm::normalize(glm::vec3{camFront.x, 0.f, camFront.z});
        m_particles.emit(ParticleSystem::Effect::ImpactSparks, m_camera.position, back, 16);
    }
    if (m_player->slidStartedThisFrame()) {
        m_audio.play(kSfxSlide);
        glm::vec3 down{0.f, -1.f, 0.f};
        m_particles.emit(ParticleSystem::Effect::ImpactSparks, m_player->eyePosition(), down, 12);
    }

    m_audio.setListener(m_camera.position, m_camera.front());

    auto& playerHealth   = m_world.get<HealthComponent>(m_playerEntity);
    int healthBeforeStep = playerHealth.current;
    // enemySystem destroys enemies whose health has dropped to <=0 this frame;
    // diffing the live count is how we detect kills for score/combo + waves.
    int enemiesBeforeStep = static_cast<int>(m_world.view<EnemyComponent>().size());
    enemySystem(m_world, m_physics, m_camera.position, m_dt, &playerHealth, &m_playerIFrames);
    int enemiesAfterStep = static_cast<int>(m_world.view<EnemyComponent>().size());
    if (playerHealth.current < healthBeforeStep) {
        m_audio.play(kSfxPlayerHit);
        m_damageFlash = kDamageFlashTime;
    }

    // Register each kill from this frame (advances score, combo and best combo).
    // EnemySystem destroys the bodies internally, so per-kill position is not
    // available here; the script callback gets the player position as the
    // best-known impact site.
    for (int i = enemiesAfterStep; i < enemiesBeforeStep; ++i) {
        registerKill(m_wave, m_waveConfig);
        m_scripts.onEnemyDeath(0, m_camera.position.x, m_camera.position.y, m_camera.position.z);
    }

    // Player death -> Dead state. Persist the run's score immediately so the
    // high score survives even if the window is closed on the death screen.
    if (!playerHealth.alive()) {
        m_playerDead = true;
        m_state      = GameState::Dead;
        recordHighScore();
        m_scripts.onPlayerDeath(m_wave.score);
        return;
    }

    WeaponComponent& weapon = currentWeapon();
    weapon.cooldown -= m_dt;
    weapon.firedThisFrame = false;
    if (m_firePressed && weapon.cooldown <= 0.f && weapon.ammo != 0) {
        m_audio.play(kSfxWeaponFire);
        // Muzzle flash at the gun, biased along the look direction.
        glm::vec3 front  = glm::normalize(m_camera.front());
        glm::vec3 muzzle = m_camera.position + front * 0.4f;
        m_particles.emit(ParticleSystem::Effect::MuzzleFlash, muzzle, front, 12);
        // Brief warm muzzle-flash light (~2 frames) to pop nearby geometry.
        spawnTransientLight(muzzle, {1.f, 0.85f, 0.5f}, 6.f, 4.f, 0.05f);
        fireWeapon();
        weapon.cooldown       = 1.f / weapon.fireRate;
        weapon.firedThisFrame = true;
        if (weapon.ammo > 0)
            --weapon.ammo;
    }
    m_firePressed = false;

    // Advance in-flight projectiles: integrate, resolve hits + splash, and spawn
    // explosion VFX/light on detonation. Particles/lights always exist here, so
    // the impact callback is safe to wire unconditionally.
    projectileSystem(m_world, m_physics, m_dt, [this](const ProjectileImpact& hit) {
        if (hit.splashRadius > 0.f) {
            // Rocket: fireball + bright transient light scaled to the blast.
            m_particles.emit(ParticleSystem::Effect::Explosion, hit.position, hit.normal, 48);
            spawnTransientLight(hit.position, {1.f, 0.6f, 0.25f}, hit.splashRadius * 1.5f, 6.f, 0.25f);
        } else {
            // Plasma / non-splash: small spark pop.
            m_particles.emit(ParticleSystem::Effect::ImpactSparks, hit.position, hit.normal, 16);
            spawnTransientLight(hit.position, {0.4f, 1.f, 0.6f}, 3.f, 3.f, 0.1f);
        }
        if (hit.hitEnemy)
            m_audio.playAt(kSfxEnemyHit, hit.position);
    });

    m_particles.update(m_dt);
    updateLights(m_dt);

    updateWaves();
}

void Engine::updateWaves() {
    // Advance combo / intermission / survival timers.
    tickWave(m_wave, m_dt);

    // Keep aliveEnemies in sync with the world (kills are also decremented in
    // registerKill, but this guards against any external destruction).
    m_wave.aliveEnemies = static_cast<int>(m_world.view<EnemyComponent>().size());

    if (m_wave.aliveEnemies > 0 || m_wave.cleared)
        return;

    // No live enemies. m_wave.intermission > 0 means the countdown to the next
    // wave is already running (armed below on the frame the wave clears).
    if (m_wave.intermission > 0.f)
        return;

    if (!m_wave.intermissionArmed) {
        // First clear frame: arm the intermission delay, then wait for it to
        // elapse via tickWave on subsequent frames.
        m_wave.intermission      = m_waveConfig.intermissionTime;
        m_wave.intermissionArmed = true;
        return;
    }

    // Intermission has elapsed: advance to the next wave (or win).
    m_wave.intermissionArmed = false;
    advanceWave(m_wave, m_waveConfig);
    if (m_wave.cleared) {
        m_state = GameState::Victory;
        recordHighScore();
        return;
    }
    if (m_wave.spawnPending) {
        int n = enemiesForWave(m_wave.wave, m_waveConfig);
        spawnWaveEnemies(n);
        m_wave.aliveEnemies = n;
        m_wave.spawnPending = false;
        m_scripts.onWaveStart(m_wave.wave);
    }
}

void Engine::render() {
    DS_ZONE();
    rhi::IRHICommandList* cmd = m_device->beginFrame();
    if (!cmd)
        return;

    rhi::ColorAttachment color{};
    color.loadOp        = rhi::LoadOp::Clear;
    color.storeOp       = rhi::StoreOp::Store;
    color.clearColor[0] = 0.05f;
    color.clearColor[1] = 0.05f;
    color.clearColor[2] = 0.08f;
    color.clearColor[3] = 1.0f;

    rhi::DepthAttachment depth{};
    depth.texture    = m_depthTexture;
    depth.loadOp     = rhi::LoadOp::Clear;
    depth.storeOp    = rhi::StoreOp::DontCare;
    depth.clearDepth = 1.0f;

    rhi::RenderPassDesc pass{};
    pass.colorAttachments = {&color, 1};
    pass.depthAttachment  = &depth;

    cmd->beginRenderPass(pass);

    float aspect       = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
    glm::mat4 viewProj = m_camera.projMatrix(aspect) * m_camera.viewMatrix();

    // Always draw the 3D scene so the world is visible behind paused overlays
    // (Dead / Victory) and so the Menu has a backdrop.
    RenderContext ctx{cmd, m_meshPipeline, viewProj, &m_lightBuffer};
    renderSystem(m_world, ctx);

    renderParticles(cmd, viewProj);

    // The combat HUD (health/ammo/crosshair) only makes sense while playing;
    // the other states draw their own overlay instead.
    if (m_state == GameState::Playing)
        renderHUD(cmd);
    else
        renderStateOverlay(cmd);

    cmd->endRenderPass();

    m_device->submitFrame(cmd);
}

void Engine::renderStateOverlay(rhi::IRHICommandList* cmd) {
    const float w = static_cast<float>(m_windowWidth);
    const float h = static_cast<float>(m_windowHeight);

    m_ui.begin(m_windowWidth, m_windowHeight);

    // Dim the scene behind the overlay text.
    m_ui.drawQuad(0.f, 0.f, w, h, {0.f, 0.f, 0.f, 0.6f});

    const char* title    = "DOOMSCROLLER";
    glm::vec4 titleColor = {0.9f, 0.95f, 1.f, 1.f};
    const char* prompt   = "CLICK or ENTER to PLAY";
    switch (m_state) {
    case GameState::Dead:
        title      = "YOU DIED";
        titleColor = {1.f, 0.15f, 0.15f, 1.f};
        prompt     = "CLICK or ENTER to RETRY";
        break;
    case GameState::Victory:
        title      = "VICTORY";
        titleColor = {0.3f, 1.f, 0.4f, 1.f};
        prompt     = "CLICK or ENTER to PLAY AGAIN";
        break;
    case GameState::Menu:
    default:
        break;
    }

    // Title (centered, large).
    {
        float scale = 7.f;
        float tw    = m_ui.textWidth(title, scale);
        float th    = m_ui.textHeight(title, scale);
        m_ui.drawText((w - tw) * 0.5f, h * 0.30f - th * 0.5f, title, scale, titleColor);
    }

    // Score readout (hidden on the fresh-start menu where score is 0).
    if (m_state != GameState::Menu) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "SCORE %d   WAVE %d   KILLS %d   BEST COMBO x%d", m_wave.score, m_wave.wave,
                      m_wave.kills, m_wave.bestCombo);
        float scale = 2.5f;
        float tw    = m_ui.textWidth(buf, scale);
        m_ui.drawText((w - tw) * 0.5f, h * 0.48f, buf, scale, {1.f, 0.95f, 0.7f, 1.f});
    }

    // High score.
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "HIGH SCORE %d", m_highScore);
        float scale = 2.5f;
        float tw    = m_ui.textWidth(buf, scale);
        m_ui.drawText((w - tw) * 0.5f, h * 0.56f, buf, scale, {0.8f, 0.85f, 1.f, 1.f});
    }

    // Restart / start prompt.
    {
        float scale = 3.f;
        float tw    = m_ui.textWidth(prompt, scale);
        m_ui.drawText((w - tw) * 0.5f, h * 0.70f, prompt, scale, {1.f, 1.f, 1.f, 1.f});
    }

    m_ui.flush(*m_device, cmd);
}

void Engine::renderParticles(rhi::IRHICommandList* cmd, const glm::mat4& viewProj) {
    m_particles.buildInstances();
    const auto& alpha    = m_particles.alphaInstances();
    const auto& additive = m_particles.additiveInstances();
    if (alpha.empty() && additive.empty())
        return;

    // Push constants: viewProj + camera right/up for billboarding.
    struct ParticlePush {
        glm::mat4 viewProj;
        glm::vec4 camRight;
        glm::vec4 camUp;
    } push;
    push.viewProj   = viewProj;
    glm::vec3 right = m_camera.right();
    glm::vec3 fwd   = m_camera.front();
    glm::vec3 up    = glm::normalize(glm::cross(right, fwd));
    push.camRight   = glm::vec4(right, 0.f);
    push.camUp      = glm::vec4(up, 0.f);

    // Alpha and additive buckets are packed back-to-back in the shared instance
    // buffer to keep uploads to a single call.
    uint32_t alphaCount    = static_cast<uint32_t>(alpha.size());
    uint32_t additiveCount = static_cast<uint32_t>(additive.size());
    uint64_t stride        = sizeof(ParticleSystem::Instance);

    if (alphaCount > 0)
        m_device->uploadImmediate(m_particleInstanceBuffer, alpha.data(), alphaCount * stride, 0);
    if (additiveCount > 0)
        m_device->uploadImmediate(m_particleInstanceBuffer, additive.data(), additiveCount * stride,
                                  alphaCount * stride);

    // Alpha bucket (blood / smoke).
    if (alphaCount > 0) {
        cmd->setPipeline(m_particleAlphaPipe);
        cmd->pushVertexConstants(&push, sizeof(push));
        cmd->setVertexBuffer(0, m_particleInstanceBuffer, 0);
        cmd->draw(6, alphaCount, 0, 0);
    }

    // Additive bucket (muzzle flash / sparks / explosion).
    if (additiveCount > 0) {
        cmd->setPipeline(m_particleAdditivePipe);
        cmd->pushVertexConstants(&push, sizeof(push));
        cmd->setVertexBuffer(0, m_particleInstanceBuffer, alphaCount * stride);
        cmd->draw(6, additiveCount, 0, 0);
    }
}

void Engine::spawnTransientLight(const glm::vec3& position, const glm::vec3& color, float radius, float intensity,
                                 float lifetime) {
    m_transientLights.push_back(TransientLight{position, color, radius, intensity, lifetime, lifetime});
}

void Engine::updateLights(float dt) {
    // Age transient lights and drop the dead ones (swap-erase, order irrelevant).
    for (size_t i = 0; i < m_transientLights.size();) {
        m_transientLights[i].life -= dt;
        if (m_transientLights[i].life <= 0.f) {
            m_transientLights[i] = m_transientLights.back();
            m_transientLights.pop_back();
        } else {
            ++i;
        }
    }

    // Gather persistent world lights first, then transients, capped at the
    // shader array size.
    int count = 0;
    auto push = [&](const glm::vec3& pos, const glm::vec3& color, float radius, float intensity) {
        if (count >= kMaxForwardLights)
            return;
        m_lightBuffer.lights[count].posRadius      = glm::vec4(pos, radius);
        m_lightBuffer.lights[count].colorIntensity = glm::vec4(color, intensity);
        ++count;
    };

    // A LightComponent uses its own position; if the entity also has a Transform
    // we follow that so attached lights move with their owner.
    auto view = m_world.view<LightComponent>();
    for (auto [entity, light] : view.each()) {
        glm::vec3 pos = light.position;
        if (auto* t = m_world.try_get<Transform>(entity))
            pos = t->position;
        push(pos, light.color, light.radius, light.intensity);
    }

    for (const auto& tl : m_transientLights) {
        // Fade intensity out over the lifetime so flashes don't pop off.
        float fade = tl.maxLife > 0.f ? (tl.life / tl.maxLife) : 1.f;
        push(tl.position, tl.color, tl.radius, tl.intensity * fade);
    }

    m_lightBuffer.count = count;
}

void Engine::renderHUD(rhi::IRHICommandList* cmd) {
    const float w = static_cast<float>(m_windowWidth);
    const float h = static_cast<float>(m_windowHeight);

    m_ui.begin(m_windowWidth, m_windowHeight);

    const auto& health   = m_world.get<HealthComponent>(m_playerEntity);
    float healthFrac     = health.max > 0 ? static_cast<float>(health.current) / static_cast<float>(health.max) : 0.f;
    healthFrac           = std::clamp(healthFrac, 0.f, 1.f);
    const bool lowHealth = healthFrac <= 0.3f;

    // Live enemy count for the wave readout.
    int enemyCount = static_cast<int>(m_world.view<EnemyComponent>().size());

    // --- Full-screen damage flash (red, alpha from the decay timer). -------
    if (m_damageFlash > 0.f) {
        float a = (m_damageFlash / kDamageFlashTime) * 0.45f;
        m_ui.drawQuad(0.f, 0.f, w, h, {0.8f, 0.f, 0.f, a});
    }

    // --- Low-health pulse: gentle red vignette via sin(time), no RNG. ------
    if (lowHealth && health.alive()) {
        float pulse = 0.18f + 0.12f * (0.5f + 0.5f * std::sin(m_timeAccum * 6.f));
        m_ui.drawQuad(0.f, 0.f, w, h, {0.7f, 0.f, 0.f, pulse});
    }

    // --- Crosshair (4 arms around a center gap). ---------------------------
    {
        const float cx = w * 0.5f, cy = h * 0.5f;
        const float len = 10.f, thick = 2.f, gap = 4.f;
        glm::vec4 col{1.f, 1.f, 1.f, 0.85f};
        m_ui.drawQuad(cx - gap - len, cy - thick * 0.5f, len, thick, col); // left
        m_ui.drawQuad(cx + gap, cy - thick * 0.5f, len, thick, col);       // right
        m_ui.drawQuad(cx - thick * 0.5f, cy - gap - len, thick, len, col); // top
        m_ui.drawQuad(cx - thick * 0.5f, cy + gap, thick, len, col);       // bottom
    }

    // --- Health bar (bottom-left). -----------------------------------------
    {
        const float bx = 24.f, by = h - 48.f, bw = 280.f, bh = 24.f;
        m_ui.drawQuad(bx - 2.f, by - 2.f, bw + 4.f, bh + 4.f, {0.f, 0.f, 0.f, 0.6f}); // backing
        m_ui.drawQuad(bx, by, bw, bh, {0.15f, 0.15f, 0.15f, 0.8f});                   // track
        glm::vec4 fill = lowHealth ? glm::vec4{0.9f, 0.2f, 0.1f, 0.95f} : glm::vec4{0.2f, 0.85f, 0.3f, 0.95f};
        m_ui.drawQuad(bx, by, bw * healthFrac, bh, fill);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "HP %d", health.current);
        m_ui.drawText(bx + 6.f, by + 4.f, buf, 2.f, {1.f, 1.f, 1.f, 1.f});
    }

    // --- Weapon / ammo readout (bottom-right). -----------------------------
    {
        const WeaponComponent& weapon = m_weapons[m_weaponIndex];
        const char* name              = weapon.type == WeaponType::Rocket   ? "ROCKET"
                                        : weapon.type == WeaponType::Plasma ? "PLASMA"
                                                                            : "RIFLE";
        char buf[96];
        if (weapon.ammo < 0)
            std::snprintf(buf, sizeof(buf), "%zu:%s  DMG %d  RPS %.0f", m_weaponIndex + 1, name, weapon.damage,
                          weapon.fireRate);
        else
            std::snprintf(buf, sizeof(buf), "%zu:%s  DMG %d  AMMO %d", m_weaponIndex + 1, name, weapon.damage,
                          weapon.ammo);
        float scale = 2.f;
        float tw    = m_ui.textWidth(buf, scale);
        m_ui.drawText(w - tw - 24.f, h - 40.f, buf, scale, {1.f, 0.85f, 0.4f, 1.f});
    }

    // --- Wave / enemy count (top-left). ------------------------------------
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "WAVE %d   ENEMIES %d", m_wave.wave, enemyCount);
        m_ui.drawText(24.f, 24.f, buf, 2.f, {0.9f, 0.95f, 1.f, 1.f});
    }

    // --- Score + combo (top-right). ----------------------------------------
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "SCORE %d", m_wave.score);
        float scale = 2.f;
        float tw    = m_ui.textWidth(buf, scale);
        m_ui.drawText(w - tw - 24.f, 24.f, buf, scale, {1.f, 0.95f, 0.7f, 1.f});

        // Show the live combo multiplier while a chain is active.
        if (m_wave.combo > 1) {
            char cbuf[32];
            std::snprintf(cbuf, sizeof(cbuf), "COMBO x%d", m_wave.combo);
            float cw = m_ui.textWidth(cbuf, scale);
            m_ui.drawText(w - cw - 24.f, 24.f + 8.f * scale + 4.f, cbuf, scale, {1.f, 0.6f, 0.2f, 1.f});
        }
    }

    // --- Intermission banner between waves. --------------------------------
    if (m_wave.intermissionArmed && m_wave.intermission > 0.f) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "WAVE %d INCOMING...", m_wave.wave + 1);
        float scale = 3.f;
        float tw    = m_ui.textWidth(buf, scale);
        m_ui.drawText((w - tw) * 0.5f, h * 0.35f, buf, scale, {0.9f, 0.95f, 1.f, 1.f});
    }

    m_ui.flush(*m_device, cmd);
}

} // namespace ds
