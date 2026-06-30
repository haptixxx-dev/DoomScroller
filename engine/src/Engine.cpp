#include "engine/Engine.h"

#include "engine/HighScore.h"
#include "engine/InstanceBatch.h"
#include "engine/LevelLoader.h"
#include "engine/Paths.h"
#include "engine/PlayerController.h"
#include "engine/Profiler.h"
#include "engine/ShaderLoader.h"
#include "engine/ShadowMatrix.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/EnemyArchetype.h"
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
#include <unordered_map>
#include <vector>

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

// Shared mesh vertex layout (task 28). Binding 0 = per-vertex pos/color/uv/normal
// (interleaved Vertex); binding 1 = per-INSTANCE model matrix streamed as four
// float4 columns (locations 4..7, stride sizeof(mat4)=64, instanced input rate).
// Both the mesh and depth-only shadow pipelines use this identical layout so the
// same vertex + instance buffers feed both passes. attrsOut must hold 8 entries;
// bindingsOut must hold 2.
static void buildMeshVertexLayout(rhi::VertexAttribute (&attrsOut)[8], rhi::VertexBinding (&bindingsOut)[2]) {
    attrsOut[0].location     = 0;
    attrsOut[0].binding      = 0;
    attrsOut[0].offset       = offsetof(Vertex, pos);
    attrsOut[0].elementCount = 3;
    attrsOut[0].isFloat      = true;
    attrsOut[1].location     = 1;
    attrsOut[1].binding      = 0;
    attrsOut[1].offset       = offsetof(Vertex, color);
    attrsOut[1].elementCount = 3;
    attrsOut[1].isFloat      = true;
    attrsOut[2].location     = 2;
    attrsOut[2].binding      = 0;
    attrsOut[2].offset       = offsetof(Vertex, uv);
    attrsOut[2].elementCount = 2;
    attrsOut[2].isFloat      = true;
    attrsOut[3].location     = 3;
    attrsOut[3].binding      = 0;
    attrsOut[3].offset       = offsetof(Vertex, normal);
    attrsOut[3].elementCount = 3;
    attrsOut[3].isFloat      = true;

    // Per-instance model matrix columns: 4 x float4 at binding 1, offsets
    // 0/16/32/48. A glm::mat4 is column-major, so column c lives at byte 16*c.
    for (uint32_t c = 0; c < 4; ++c) {
        attrsOut[4 + c].location     = 4 + c;
        attrsOut[4 + c].binding      = 1;
        attrsOut[4 + c].offset       = c * static_cast<uint32_t>(sizeof(float) * 4);
        attrsOut[4 + c].elementCount = 4;
        attrsOut[4 + c].isFloat      = true;
    }

    bindingsOut[0].binding   = 0;
    bindingsOut[0].stride    = static_cast<uint32_t>(sizeof(Vertex));
    bindingsOut[0].instanced = false;
    bindingsOut[1].binding   = 1;
    bindingsOut[1].stride    = static_cast<uint32_t>(sizeof(glm::mat4));
    bindingsOut[1].instanced = true;
}

void Engine::onMeshDestroyed(entt::registry& reg, entt::entity e) {
    const auto& mesh = reg.get<MeshComponent>(e);
    if (mesh.vertexBuffer.valid())
        m_device->destroyBuffer(mesh.vertexBuffer);
    if (mesh.indexBuffer.valid())
        m_device->destroyBuffer(mesh.indexBuffer);
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

    // Quality tier (task 34): pick a render preset from the device caps once at
    // startup. Gates the shadow pass + shadow-map size + bloom RT size below.
    m_quality = profileForCaps(m_device->caps());

    // Free each entity's GPU mesh buffers when its MeshComponent is destroyed
    // (entity death, world.clear()). Must be connected before any mesh entity
    // is created. See Engine::onMeshDestroyed.
    m_world.on_destroy<MeshComponent>().connect<&Engine::onMeshDestroyed>(*this);

    m_physics.init();

    // User settings (task 38): load <userDir>/settings.cfg BEFORE the audio +
    // camera setup so the persisted volumes / look sensitivity are applied as
    // the initial state. A missing or unparseable file falls back to the
    // GameSettings defaults (same as a fresh install). The window size is fixed
    // at construction from EngineConfig, so settings.windowWidth/Height are NOT
    // applied here (no runtime resize path yet) — they round-trip on disk only.
    m_settings               = loadSettings(paths::userDir()).value_or(GameSettings{});
    m_camera.lookSensitivity = m_settings.lookSensitivity;

    // Audio is non-fatal: if the device fails to open, AudioSystem becomes a
    // silent no-op and the rest of the engine is unaffected. Apply the persisted
    // volumes right after init so the first frame already honors them.
    m_audio.init();
    m_audio.setMasterVolume(m_settings.masterVolume);
    m_audio.setSfxVolume(m_settings.sfxVolume);
    m_audio.setMusicVolume(m_settings.musicVolume);
    m_audio.setUiVolume(m_settings.uiVolume);
    m_audio.playMusic(kMusicTrack);

    initScene();

    // Lua data-driven tuning (waves + enemy stats). Must run after m_waveConfig
    // holds its hardcoded defaults so the script only overrides what it sets.
    initScripts();

    // Persistent progression (task 38): seed m_save + m_highScore from
    // <userDir>/save.dat. The legacy single-int HighScore.dat is still read as a
    // fallback so an existing best score migrates into the new blob; the higher
    // of the two wins. Both are tolerant of a missing file (default 0 / nullopt).
    m_highScore = highscore::load(highscore::defaultPath());
    if (std::optional<SaveData> loaded = loadGame(paths::userDir()))
        m_save = *loaded;
    m_highScore      = std::max(m_highScore, static_cast<int>(m_save.highScore));
    m_save.highScore = static_cast<uint32_t>(m_highScore);

    // Start at the menu; a click / Enter begins the first run.
    m_state = GameState::Menu;

    m_lastTick = SDL_GetPerformanceCounter();
}

Engine::~Engine() {
    m_world.clear();
    m_textures->destroyAll();
    if (m_linearSampler.valid())
        m_device->destroySampler(m_linearSampler);
    if (m_instanceBuffer.valid())
        m_device->destroyBuffer(m_instanceBuffer);
    if (m_shadowInstanceBuffer.valid())
        m_device->destroyBuffer(m_shadowInstanceBuffer);
    if (m_meshPipeline.valid())
        m_device->destroyPipeline(m_meshPipeline);
    if (m_meshVS.valid())
        m_device->destroyShader(m_meshVS);
    if (m_meshFS.valid())
        m_device->destroyShader(m_meshFS);
    if (m_shadowPipeline.valid())
        m_device->destroyPipeline(m_shadowPipeline);
    if (m_shadowVS.valid())
        m_device->destroyShader(m_shadowVS);
    if (m_shadowFS.valid())
        m_device->destroyShader(m_shadowFS);
    if (m_shadowSampler.valid())
        m_device->destroySampler(m_shadowSampler);
    if (m_shadowMap.valid())
        m_device->destroyTexture(m_shadowMap);
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
    if (m_particleComputePipe.valid())
        m_device->destroyComputePipeline(m_particleComputePipe);
    if (m_particleStateBuffer.valid())
        m_device->destroyBuffer(m_particleStateBuffer);
    if (m_tonemapPipeline.valid())
        m_device->destroyPipeline(m_tonemapPipeline);
    if (m_tonemapVS.valid())
        m_device->destroyShader(m_tonemapVS);
    if (m_tonemapFS.valid())
        m_device->destroyShader(m_tonemapFS);
    if (m_brightPipeline.valid())
        m_device->destroyPipeline(m_brightPipeline);
    if (m_brightVS.valid())
        m_device->destroyShader(m_brightVS);
    if (m_brightFS.valid())
        m_device->destroyShader(m_brightFS);
    if (m_blurPipeline.valid())
        m_device->destroyPipeline(m_blurPipeline);
    if (m_blurVS.valid())
        m_device->destroyShader(m_blurVS);
    if (m_blurFS.valid())
        m_device->destroyShader(m_blurFS);
    if (m_bloomA.valid())
        m_device->destroyTexture(m_bloomA);
    if (m_bloomB.valid())
        m_device->destroyTexture(m_bloomB);
    m_ui.shutdown(*m_device);
    if (m_depthTexture.valid())
        m_device->destroyTexture(m_depthTexture);
    if (m_hdrTexture.valid())
        m_device->destroyTexture(m_hdrTexture);
    m_textures.reset();
    m_device.reset();
    if (m_window)
        SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void Engine::initScene() {
    ShaderLoader loader(static_cast<SDL_GPUDevice*>(m_device->nativeDevice()), paths::shaders());
    m_meshVS = loader.load(*m_device, "mesh", rhi::ShaderStage::Vertex, 0, 1);
    // Mesh FS now declares 2 samplers (albedo + shadow map) and 3 uniform
    // buffers (lights @0, material @1, shadow matrix @2).
    m_meshFS = loader.load(*m_device, "mesh", rhi::ShaderStage::Fragment, 2, 3);

    // Seed the runtime quality sizes (task 34): shadow-map resolution and the
    // bloom render-target size (half- vs full-window res). Done before the
    // resources below are created so they pick up the tier's sizes.
    m_shadowMapSize = static_cast<uint32_t>(m_quality.shadowMapSize);
    m_bloomWidth =
        m_quality.halfResBloom ? static_cast<uint32_t>(m_windowWidth) / 2u : static_cast<uint32_t>(m_windowWidth);
    m_bloomHeight =
        m_quality.halfResBloom ? static_cast<uint32_t>(m_windowHeight) / 2u : static_cast<uint32_t>(m_windowHeight);
    if (m_bloomWidth == 0u)
        m_bloomWidth = 1u;
    if (m_bloomHeight == 0u)
        m_bloomHeight = 1u;

    // Shadow depth pass: a vertex stage transforming by lightSpace*model (1
    // vertex uniform), and a trivial fragment (no samplers / uniforms). Skipped
    // entirely on the Minimum tier (task 34): no shadow shaders/pipeline/map are
    // built and the scene pass binds a 1x1 placeholder so the sun stays fully
    // lit (sampleSunShadow over an all-far depth returns 1.0 == lit).
    if (m_quality.shadows) {
        m_shadowVS = loader.load(*m_device, "shadow_depth", rhi::ShaderStage::Vertex, 0, 1);
        m_shadowFS = loader.load(*m_device, "shadow_depth", rhi::ShaderStage::Fragment, 0, 0);
    }

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
    if (m_depthTexture.valid())
        m_device->destroyTexture(m_depthTexture);
    m_depthTexture = m_device->createTexture(depthDesc);

    // Offscreen HDR color target (RGBA16Float). The scene + particles render
    // here; the tonemap pass resolves it to the LDR swapchain. Sized to the
    // window and (re)created here alongside the depth texture.
    rhi::TextureDesc hdrDesc{};
    hdrDesc.width          = static_cast<uint32_t>(m_windowWidth);
    hdrDesc.height         = static_cast<uint32_t>(m_windowHeight);
    hdrDesc.format         = rhi::TextureFormat::RGBA16Float;
    hdrDesc.isRenderTarget = true;
    hdrDesc.debugName      = "hdr";
    if (m_hdrTexture.valid())
        m_device->destroyTexture(m_hdrTexture);
    m_hdrTexture = m_device->createTexture(hdrDesc);

    // Bloom ping-pong targets (task 26): two RGBA16Float render targets sized to
    // the window (full-res first cut). Bright-pass -> A, blurH A -> B, blurV B ->
    // A; tonemap then adds A. Recreated alongside the HDR target on resize.
    rhi::TextureDesc bloomDesc{};
    bloomDesc.width          = m_bloomWidth;
    bloomDesc.height         = m_bloomHeight;
    bloomDesc.format         = rhi::TextureFormat::RGBA16Float;
    bloomDesc.isRenderTarget = true;
    bloomDesc.debugName      = "bloomA";
    if (m_bloomA.valid())
        m_device->destroyTexture(m_bloomA);
    m_bloomA            = m_device->createTexture(bloomDesc);
    bloomDesc.debugName = "bloomB";
    if (m_bloomB.valid())
        m_device->destroyTexture(m_bloomB);
    m_bloomB = m_device->createTexture(bloomDesc);

    // Directional sun shadow map (D32Float, square). isRenderTarget on a depth
    // texture asks the backend to add the SAMPLER usage bit so the mesh shader
    // can read it. Fixed resolution (kShadowMapSize) independent of the window.
    rhi::TextureDesc shadowDesc{};
    shadowDesc.width          = m_shadowMapSize;
    shadowDesc.height         = m_shadowMapSize;
    shadowDesc.format         = rhi::TextureFormat::D32Float;
    shadowDesc.isDepthStencil = true;
    shadowDesc.isRenderTarget = true; // also wants SAMPLER usage
    shadowDesc.debugName      = "shadowMap";
    if (!m_shadowMap.valid())
        m_shadowMap = m_device->createTexture(shadowDesc);

    // Shadow map sampler: clamp to edge so off-map lookups read the border, and
    // nearest-ish linear is fine for the manual PCF in the shader.
    if (!m_shadowSampler.valid()) {
        rhi::SamplerDesc shadowSampler{};
        shadowSampler.addressU = rhi::AddressMode::ClampToEdge;
        shadowSampler.addressV = rhi::AddressMode::ClampToEdge;
        shadowSampler.addressW = rhi::AddressMode::ClampToEdge;
        m_shadowSampler        = m_device->createSampler(shadowSampler);
    }

    // Vertex layout: interleaved pos + color + uv + normal at binding 0, plus a
    // per-instance model matrix (task 28) at binding 1 streamed as four float4
    // columns (locations 4..7, instanced). buildMeshVertexLayout fills both so
    // the mesh + shadow pipelines (and the DS_DEV hot-reload path) share one
    // definition and can never drift apart.
    rhi::VertexAttribute attrs[8];
    rhi::VertexBinding vbindings[2];
    buildMeshVertexLayout(attrs, vbindings);

    // The mesh pipeline now renders into the HDR target, not the swapchain.
    rhi::ColorTargetDesc colorTarget{};
    colorTarget.format = rhi::TextureFormat::RGBA16Float;

    rhi::PipelineDesc pipeDesc{};
    pipeDesc.vertexShader     = m_meshVS;
    pipeDesc.fragmentShader   = m_meshFS;
    pipeDesc.vertexAttributes = {attrs, 8};
    pipeDesc.vertexBindings   = {vbindings, 2};
    pipeDesc.colorTargets     = {&colorTarget, 1};
    pipeDesc.hasDepth         = true;
    pipeDesc.depthTest        = true;
    pipeDesc.depthWrite       = true;
    pipeDesc.depthFormat      = rhi::TextureFormat::D32Float;
    pipeDesc.depthCompare     = rhi::CompareOp::Less;
    pipeDesc.cullMode         = rhi::CullMode::None;
    m_meshPipeline            = m_device->createPipeline(pipeDesc);

    // Per-frame instance buffer (task 28): holds up to kMaxInstances model
    // matrices, re-uploaded each frame by renderSystem / renderDepthOnly and
    // bound at vertex slot 1 for the instanced draws.
    if (!m_instanceBuffer.valid()) {
        rhi::BufferDesc instDesc{};
        instDesc.size      = static_cast<uint64_t>(sizeof(glm::mat4)) * kMaxInstances;
        instDesc.usage     = rhi::BufferUsage::Vertex;
        instDesc.debugName = "mesh_instances";
        m_instanceBuffer   = m_device->createBuffer(instDesc);
    }
    // Shadow pass instance buffer (only when the shadow pass is built).
    if (m_quality.shadows && !m_shadowInstanceBuffer.valid()) {
        rhi::BufferDesc shInstDesc{};
        shInstDesc.size        = static_cast<uint64_t>(sizeof(glm::mat4)) * kMaxInstances;
        shInstDesc.usage       = rhi::BufferUsage::Vertex;
        shInstDesc.debugName   = "shadow_instances";
        m_shadowInstanceBuffer = m_device->createBuffer(shInstDesc);
    }

    // Depth-only shadow pipeline: NO color targets, depth test/write on, front
    // faces culled so back faces define the shadow caster depth (reduces
    // peter-panning / acne on lit surfaces). Reuses the mesh vertex layout.
    // Built only on the shadows-enabled tier (task 34).
    if (m_quality.shadows) {
        rhi::PipelineDesc shadowPipe{};
        shadowPipe.vertexShader     = m_shadowVS;
        shadowPipe.fragmentShader   = m_shadowFS;
        shadowPipe.vertexAttributes = {attrs, 8};
        shadowPipe.vertexBindings   = {vbindings, 2};
        shadowPipe.colorTargets     = {}; // empty: depth-only
        shadowPipe.hasDepth         = true;
        shadowPipe.depthTest        = true;
        shadowPipe.depthWrite       = true;
        shadowPipe.depthFormat      = rhi::TextureFormat::D32Float;
        shadowPipe.depthCompare     = rhi::CompareOp::Less;
        shadowPipe.cullMode         = rhi::CullMode::Front;
        m_shadowPipeline            = m_device->createPipeline(shadowPipe);
    }

    // Checkerboard placeholder texture
    constexpr uint32_t kTexW = 8, kTexH = 8;
    uint8_t pixels[kTexW * kTexH * 4];
    makeCheckerboard(pixels, kTexW, kTexH);
    rhi::RHITexture albedo = m_textures->createFromMemory(pixels, kTexW, kTexH, "checkerboard");

    m_camera.yaw = -90.f;

    // Prefer a data-driven level file; fall back to the hardcoded arena when it
    // is missing so the engine always has playable geometry. The level's
    // player-start spawn (flags bit0) overrides m_playerSpawn when present
    // (task 42); otherwise it stays kPlayerSpawn, matching the arena fallback.
    m_playerSpawn = kPlayerSpawn;
    if (!loadLevel(kStartupLevel, albedo, m_linearSampler, &m_playerSpawn))
        buildArena(albedo, m_linearSampler);

    // Camera starts at the resolved player spawn at eye height.
    m_camera.position = m_playerSpawn;

    // Player capsule: radius 0.4, halfHeight 0.5 (total height ~1.8 units)
    m_playerBodyId = m_physics.addCapsule(0.5f, 0.4f, m_playerSpawn);
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

    // Per-weapon upgrade stacks + accumulated mods (task 37), parallel to
    // m_weapons. Reset to identity each (re)start in startGame().
    m_weaponUpgrades.assign(m_weapons.size(), {});
    m_weaponMods.assign(m_weapons.size(), WeaponMods{});
    m_prevWeaponIndex = 0;

    initParticles();

    // Fullscreen tonemap pass: reads the HDR target and resolves it to the LDR
    // swapchain. No vertex buffer (the vertex stage builds a full-screen
    // triangle from SV_VertexID), no depth, no culling. One sampler (slot 0,
    // the HDR target) and one fragment uniform buffer (the exposure constant).
    // Tonemap FS now samples 2 textures: the HDR target (slot 0) and the bloom
    // result (slot 1). One fragment uniform buffer (exposure + bloom intensity).
    m_tonemapVS = loader.load(*m_device, "tonemap", rhi::ShaderStage::Vertex, 0, 0);
    m_tonemapFS = loader.load(*m_device, "tonemap", rhi::ShaderStage::Fragment, 2, 1);

    rhi::ColorTargetDesc tonemapColor{};
    tonemapColor.format = m_device->swapchainFormat();

    rhi::PipelineDesc tonemapDesc{};
    tonemapDesc.vertexShader   = m_tonemapVS;
    tonemapDesc.fragmentShader = m_tonemapFS;
    // Empty vertex layout: positions are generated in the shader.
    tonemapDesc.colorTargets = {&tonemapColor, 1};
    tonemapDesc.hasDepth     = false;
    tonemapDesc.depthTest    = false;
    tonemapDesc.depthWrite   = false;
    tonemapDesc.cullMode     = rhi::CullMode::None;
    m_tonemapPipeline        = m_device->createPipeline(tonemapDesc);

    // --- Bloom pipelines (task 26). ----------------------------------------
    // Bright-pass: 1 sampler (HDR slot 0), 1 fragment uniform (threshold/knee).
    // Blur: 1 sampler (source slot 0), 1 fragment uniform (step direction). Both
    // are fullscreen (empty vertex layout), no depth, no cull, blend off, and
    // write into an RGBA16Float bloom target.
    m_brightVS = loader.load(*m_device, "post", rhi::ShaderStage::Vertex, 0, 0);
    m_brightFS = loader.load(*m_device, "post", rhi::ShaderStage::Fragment, 1, 1);
    m_blurVS   = loader.load(*m_device, "blur", rhi::ShaderStage::Vertex, 0, 0);
    m_blurFS   = loader.load(*m_device, "blur", rhi::ShaderStage::Fragment, 1, 1);

    rhi::ColorTargetDesc bloomColor{};
    bloomColor.format = rhi::TextureFormat::RGBA16Float;

    rhi::PipelineDesc brightDesc{};
    brightDesc.vertexShader   = m_brightVS;
    brightDesc.fragmentShader = m_brightFS;
    brightDesc.colorTargets   = {&bloomColor, 1};
    brightDesc.hasDepth       = false;
    brightDesc.depthTest      = false;
    brightDesc.depthWrite     = false;
    brightDesc.cullMode       = rhi::CullMode::None;
    m_brightPipeline          = m_device->createPipeline(brightDesc);

    rhi::PipelineDesc blurDesc{};
    blurDesc.vertexShader   = m_blurVS;
    blurDesc.fragmentShader = m_blurFS;
    blurDesc.colorTargets   = {&bloomColor, 1};
    blurDesc.hasDepth       = false;
    blurDesc.depthTest      = false;
    blurDesc.depthWrite     = false;
    blurDesc.cullMode       = rhi::CullMode::None;
    m_blurPipeline          = m_device->createPipeline(blurDesc);

    m_ui.init(*m_device, m_device->nativeDevice(), paths::shaders());
}

#ifdef DS_DEV
void Engine::reloadShader(const std::string& name) {
    // The watcher has already rewritten the compiled bytecode in paths::shaders()
    // for `name`; here we reload the shader module(s) and reissue the pipeline(s)
    // that consume them. The mesh pipeline is wired as the concrete proof-of-path
    // (a full vertex layout + depth + shadow-map sampler binding); the remaining
    // shaders are logged so it is clear what would hook in next.
    //
    // Reissue is done create-new-then-destroy-old: createPipeline/createShader can
    // throw on a bad recompile, and doing the destroy last means a throw leaves the
    // engine running on the previous (valid) handles instead of dangling ones.
    if (name != "mesh") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[hot-reload] shader '%s' recompiled; pipeline reissue not wired (mesh only)", name.c_str());
        return;
    }

    ShaderLoader loader(static_cast<SDL_GPUDevice*>(m_device->nativeDevice()), paths::shaders());
    // Declared outside the try so the catch can destroy any handle that was
    // created before a later step threw (otherwise a partial reload leaks GPU
    // shader modules on every failed recompile-and-retry).
    rhi::RHIShader newVS     = {};
    rhi::RHIShader newFS     = {};
    rhi::RHIPipeline newPipe = {};
    bool swapped             = false;
    try {
        newVS = loader.load(*m_device, "mesh", rhi::ShaderStage::Vertex, 0, 1);
        newFS = loader.load(*m_device, "mesh", rhi::ShaderStage::Fragment, 2, 3);

        // Mesh vertex layout (must match initScene): per-vertex stream at binding
        // 0 + per-instance model matrix at binding 1 (task 28). Shared helper so
        // the hot-reload pipeline never drifts from the original.
        rhi::VertexAttribute attrs[8];
        rhi::VertexBinding vbindings[2];
        buildMeshVertexLayout(attrs, vbindings);

        rhi::ColorTargetDesc colorTarget{};
        colorTarget.format = rhi::TextureFormat::RGBA16Float;

        rhi::PipelineDesc pipeDesc{};
        pipeDesc.vertexShader     = newVS;
        pipeDesc.fragmentShader   = newFS;
        pipeDesc.vertexAttributes = {attrs, 8};
        pipeDesc.vertexBindings   = {vbindings, 2};
        pipeDesc.colorTargets     = {&colorTarget, 1};
        pipeDesc.hasDepth         = true;
        pipeDesc.depthTest        = true;
        pipeDesc.depthWrite       = true;
        pipeDesc.depthFormat      = rhi::TextureFormat::D32Float;
        pipeDesc.depthCompare     = rhi::CompareOp::Less;
        pipeDesc.cullMode         = rhi::CullMode::None;
        newPipe                   = m_device->createPipeline(pipeDesc);

        // Swap in the new handles, then destroy the old ones.
        rhi::RHIPipeline oldPipe = m_meshPipeline;
        rhi::RHIShader oldVS     = m_meshVS;
        rhi::RHIShader oldFS     = m_meshFS;
        m_meshPipeline           = newPipe;
        m_meshVS                 = newVS;
        m_meshFS                 = newFS;
        swapped                  = true;
        if (oldPipe.valid())
            m_device->destroyPipeline(oldPipe);
        if (oldVS.valid())
            m_device->destroyShader(oldVS);
        if (oldFS.valid())
            m_device->destroyShader(oldFS);

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[hot-reload] mesh pipeline reissued");
    } catch (const std::exception& e) {
        // Keep running on the old pipeline; the bad shader can be fixed and saved
        // again to retry. Destroy anything we created before the throw so a
        // failed reload does not leak GPU resources.
        if (!swapped) {
            if (newPipe.valid())
                m_device->destroyPipeline(newPipe);
            if (newVS.valid())
                m_device->destroyShader(newVS);
            if (newFS.valid())
                m_device->destroyShader(newFS);
        }
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[hot-reload] mesh reload failed: %s", e.what());
    }
}
#endif

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
    alphaTarget.format             = rhi::TextureFormat::RGBA16Float;
    alphaTarget.blend.blendEnabled = true;
    alphaTarget.blend.srcColor     = rhi::BlendFactor::One;
    alphaTarget.blend.dstColor     = rhi::BlendFactor::OneMinusSrcAlpha;
    alphaTarget.blend.colorOp      = rhi::BlendOp::Add;
    alphaTarget.blend.srcAlpha     = rhi::BlendFactor::One;
    alphaTarget.blend.dstAlpha     = rhi::BlendFactor::OneMinusSrcAlpha;
    alphaTarget.blend.alphaOp      = rhi::BlendOp::Add;

    rhi::ColorTargetDesc additiveTarget{};
    additiveTarget.format             = rhi::TextureFormat::RGBA16Float;
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

    // Dynamic per-frame instance buffer, sized for the full pool. On the
    // Enhanced tier (compute particles) it is ALSO a compute-write target so the
    // GPU sim can fill it directly; on Minimum it is a plain vertex buffer the
    // CPU uploads into each frame.
    rhi::BufferDesc instDesc{};
    instDesc.size            = sizeof(ParticleSystem::Instance) * kMaxParticlesGPU;
    instDesc.usage           = m_quality.computeParticles ? (rhi::BufferUsage::Vertex | rhi::BufferUsage::StorageWrite)
                                                          : rhi::BufferUsage::Vertex;
    instDesc.debugName       = "particle_instances";
    m_particleInstanceBuffer = m_device->createBuffer(instDesc);

    // --- GPU compute particle sim (task 39). -------------------------------
    // Only attempt the compute path on the Enhanced tier. If the pipeline fails
    // to compile/create (e.g. backend without compute), m_computeParticles stays
    // false and renderParticles() uses the CPU upload path unchanged.
    if (m_quality.computeParticles) {
        std::vector<uint8_t> compBytes;
        if (loader.loadBytecode("particle_sim", rhi::ShaderStage::Compute, compBytes) && !compBytes.empty()) {
            rhi::ComputePipelineDesc cdesc{};
            cdesc.format                  = loader.format();
            cdesc.bytecode                = compBytes.data();
            cdesc.bytecodeSize            = compBytes.size();
            cdesc.entryPoint              = "compMain";
            cdesc.numReadOnlyStorageBufs  = 1; // particle state
            cdesc.numReadWriteStorageBufs = 1; // instance output
            cdesc.numUniformBuffers       = 1; // SimParams
            cdesc.threadCountX            = 64;
            cdesc.debugName               = "particle_sim";
            m_particleComputePipe         = m_device->createComputePipeline(cdesc);
        }

        if (m_particleComputePipe.valid()) {
            rhi::BufferDesc stateDesc{};
            stateDesc.size        = sizeof(ParticleSystem::GpuParticle) * kMaxParticlesGPU;
            stateDesc.usage       = rhi::BufferUsage::Storage;
            stateDesc.debugName   = "particle_state";
            m_particleStateBuffer = m_device->createBuffer(stateDesc);
            m_computeParticles    = m_particleStateBuffer.valid();
        }
        if (!m_computeParticles)
            SDL_LogWarn(SDL_LOG_CATEGORY_GPU,
                        "GPU compute particles requested but unavailable; using CPU particle path");
    }
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
    // out across the arena instead of stacking on one corner. Archetype varies
    // by wave + index so later waves field Chargers and Ranged enemies too.
    for (int i = 0; i < count; ++i)
        spawnEnemy(spots[i % spots.size()], m_enemyAlbedo, m_enemySampler, archetypeForWave(m_wave.wave, i));
}

void Engine::respawnPlayer() {
    auto& health   = m_world.get<HealthComponent>(m_playerEntity);
    health.current = health.max;

    m_physics.setPosition(m_playerBodyId, m_playerSpawn);
    m_physics.setLinearVelocity(m_playerBodyId, {0.f, 0.f, 0.f});
    m_camera.position = m_playerSpawn;

    m_playerDead    = false;
    m_playerIFrames = 0.f;
}

void Engine::startGame() {
    // Clear any leftover gameplay entities (enemies + in-flight projectiles +
    // gibs) from a previous run; the arena geometry, player and lights persist.
    // clearPhysicsActors first frees the enemy capsules + gib bodies in Jolt so
    // bodies don't accumulate against the 1024-body cap across runs (task 36).
    {
        clearPhysicsActors();
        auto enemies = m_world.view<EnemyComponent>();
        m_world.destroy(enemies.begin(), enemies.end());
        auto projectiles = m_world.view<ProjectileComponent>();
        m_world.destroy(projectiles.begin(), projectiles.end());
        // Uncollected pickups from a prior run (task 33).
        auto pickups = m_world.view<PickupComponent>();
        m_world.destroy(pickups.begin(), pickups.end());
    }

    respawnPlayer();

    resetWave(m_wave);
    m_weaponIndex = 0;
    m_damageFlash = 0.f;
    m_killCount   = 0;

    // Lifetime run counter (task 38): one run begins here. Persisted on the next
    // run-end via persistSave(); not flushed immediately to avoid a disk write
    // on every (re)start.
    ++m_save.totalRuns;

    // Reset run-scoped systems: weapon upgrades, style meter, game-feel state,
    // parry, and combat-feedback containers (task 25/29/32/35/37).
    m_weaponUpgrades.assign(m_weapons.size(), {});
    m_weaponMods.assign(m_weapons.size(), WeaponMods{});
    m_lastUpgradeWave        = 0;
    m_prevWeaponIndex        = 0;
    m_weaponSwitchedRecently = false;
    m_weaponSwitchTimer      = 0.f;
    m_style                  = StyleState{};
    m_screenShake            = ScreenShake{};
    m_recoil                 = Recoil{};
    m_hitstop                = Hitstop{};
    m_parry                  = ParryState{};
    m_parryFlash             = 0.f;
    m_parryPressed           = false;
    m_altFirePressed         = false;
    m_damageEvents.clear();
    m_hitMarker = HitMarker{};

    // Kick off the first wave immediately (no intermission before wave 1).
    advanceWave(m_wave, m_waveConfig);
    if (m_wave.spawnPending) {
        int n = enemiesForWave(m_wave.wave, m_waveConfig);
        spawnWaveEnemies(n);
        m_wave.aliveEnemies = n;
        m_wave.spawnPending = false;
        m_scripts.onWaveStart(m_wave.wave);
    }

    // Close the settings panel + restore any music duck from a death/victory
    // screen, and reset the per-run feedback state (footsteps + rank tracking).
    m_settingsOpen  = false;
    m_activeSlider  = -1;
    m_footstepTimer = 0.f;
    m_prevRank      = m_style.rank;
    m_audio.duckMusic(1.f);

    m_state = GameState::Playing;
}

void Engine::recordHighScore() {
    if (highscore::save(highscore::defaultPath(), m_wave.score))
        m_highScore = m_wave.score;
    else
        m_highScore = std::max(m_highScore, m_wave.score);
    // Generalised persistence (task 38): also fold this run into the save blob
    // (best wave / kills / combo + lifetime totals) and write it out.
    persistSave();
}

void Engine::persistSave() {
    // Accumulate the just-finished run into the lifetime progression record.
    // bestWave / highScore / bestCombo are maxima; totalKills accumulates this
    // run's kills (totalRuns is bumped at startGame, not here). m_highScore was
    // already reconciled by recordHighScore, so mirror it back.
    m_save.bestWave  = std::max(m_save.bestWave, static_cast<uint32_t>(std::max(m_wave.wave, 0)));
    m_save.highScore = static_cast<uint32_t>(std::max(m_highScore, 0));
    m_save.bestCombo = std::max(m_save.bestCombo, static_cast<uint32_t>(std::max(m_wave.bestCombo, 0)));
    m_save.totalKills += static_cast<uint32_t>(std::max(m_wave.kills, 0));

    // saveGame creates the userDir parent tree lazily; failures are swallowed
    // (persisting progression is non-fatal, mirroring the high-score writer).
    (void)saveGame(paths::userDir(), m_save);
}

void Engine::fireHitscanRay(const WeaponComponent& weapon, const glm::vec3& origin, const glm::vec3& dir) {
    glm::vec3 hitPoint{0.f};
    uint32_t hitId = m_physics.castRay(origin, dir, 100.f, m_playerBodyId, hitPoint);
    if (hitId == UINT32_MAX)
        return;

    auto view = m_world.view<EnemyComponent, Transform>();
    for (auto [entity, enemy, transform] : view.each()) {
        if (enemy.physicsBodyId == hitId) {
            enemy.health -= weapon.damage;
            bool killed = enemy.health <= 0;
            // Death is handled by enemySystem next frame; play the death cue now
            // (positioned at the enemy) and otherwise an impact cue.
            if (killed)
                m_audio.playAt(kSfxEnemyDeath, transform.position);
            else
                m_audio.playAt(kSfxEnemyHit, transform.position);
            // Blood sprays back toward the shooter.
            m_particles.emit(ParticleSystem::Effect::BloodBurst, hitPoint, -dir, 24);
            // Floating damage number + crosshair hit marker (task 29).
            addDamageEvent(m_damageEvents, hitPoint, weapon.damage, killed);
            triggerHitMarker(m_hitMarker, killed);
            return;
        }
    }

    // Boss hit (task 40): the boss is not an EnemyComponent, so resolve it here
    // against its capsule body id. Damage feeds its HealthComponent (the bar).
    auto bossView = m_world.view<BossComponent, HealthComponent, Transform>();
    for (auto [entity, boss, health, transform] : bossView.each()) {
        if (boss.physicsBodyId != hitId)
            continue;
        // Extra punish during the parryable vulnerable window.
        int dmg = boss.vulnerableTimer > 0.f ? weapon.damage * 2 : weapon.damage;
        health.current -= dmg;
        bool killed = health.current <= 0;
        m_audio.playAt(killed ? kSfxEnemyDeath : kSfxEnemyHit, transform.position);
        m_particles.emit(ParticleSystem::Effect::BloodBurst, hitPoint, -dir, 24);
        addDamageEvent(m_damageEvents, hitPoint, dmg, killed);
        triggerHitMarker(m_hitMarker, killed);
        return;
    }

    // Hit geometry (wall/floor/ceiling): kick sparks off the surface back
    // toward the shooter.
    m_particles.emit(ParticleSystem::Effect::ImpactSparks, hitPoint, -dir, 16);
}

void Engine::fireWeaponPrimary(const WeaponComponent& weapon) {
    glm::vec3 origin = m_camera.position;
    glm::vec3 dir    = glm::normalize(m_camera.front());

    // Projectile weapons spawn a flying entity; the rest of the hit/damage
    // resolution happens in projectileSystem during update().
    if (weapon.type != WeaponType::Hitscan) {
        spawnProjectile(weapon, origin, dir);
        return;
    }
    fireHitscanRay(weapon, origin, dir);
}

void Engine::fireWeaponAlt(const WeaponComponent& weapon) {
    glm::vec3 origin = m_camera.position;
    glm::vec3 dir    = glm::normalize(m_camera.front());
    AltFireMode mode = altFireFor(weapon.type);
    int pellets      = altFirePelletCount(mode);
    float spread     = altFireSpreadRadians(mode);

    if (mode == AltFireMode::ShotgunSpread) {
        // Fan hitscan pellets within a cone of half-angle `spread` around dir.
        // A simple orthonormal basis off dir scatters each pellet; the LCG seed
        // is per-shot so the pattern varies but needs no engine RNG state.
        glm::vec3 up    = std::abs(dir.y) < 0.99f ? glm::vec3{0.f, 1.f, 0.f} : glm::vec3{1.f, 0.f, 0.f};
        glm::vec3 right = glm::normalize(glm::cross(dir, up));
        up              = glm::normalize(glm::cross(right, dir));
        uint32_t seed   = static_cast<uint32_t>(m_timeAccum * 100000.f) | 1u;
        auto rnd        = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24);
        };
        for (int i = 0; i < pellets; ++i) {
            float az    = rnd() * 2.f * 3.14159265f;
            float ang   = rnd() * spread;
            glm::vec3 d = glm::normalize(dir + (std::cos(az) * right + std::sin(az) * up) * std::tan(ang));
            fireHitscanRay(weapon, origin, d);
        }
        return;
    }

    // ChargedRocket / PlasmaOverheat: a single buffed shot. Double the damage
    // (and splash) via a transient damage upgrade folded over the effective wpn.
    WeaponComponent buffed = withMods(weapon, WeaponMods{2.f, 1.f, 1.5f, 1.f});
    if (buffed.type != WeaponType::Hitscan)
        spawnProjectile(buffed, origin, dir);
    else
        fireHitscanRay(buffed, origin, dir);
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

void Engine::spawnEnemyProjectile(const glm::vec3& origin, const glm::vec3& velocity, int damage,
                                  uint32_t ownerBodyId) {
    // Small red bolt; reuses the projectile material. ownerBodyId is the firing
    // enemy so the hit ray ignores it (and the player parry can re-own it).
    constexpr float half = 0.1f;
    glm::vec3 color{1.f, 0.3f, 0.2f};

    auto e = m_world.create();
    Transform t{};
    t.position = origin;
    m_world.emplace<Transform>(e, t);
    m_world.emplace<MeshComponent>(e, makeBoxMesh(*m_device, half, half, half, color));
    m_world.emplace<MaterialComponent>(e, MaterialComponent{m_projectileAlbedo, m_projectileSampler});

    ProjectileComponent pc{};
    pc.velocity     = velocity;
    pc.damage       = damage;
    pc.lifetime     = 4.f;
    pc.splashRadius = 0.f;
    pc.ownerBodyId  = ownerBodyId;
    m_world.emplace<ProjectileComponent>(e, pc);

    spawnTransientLight(origin, color, 3.f, 2.f, 0.1f);
}

void Engine::processEnemyProjectiles() {
    const glm::vec3 playerPos   = m_camera.position;
    const bool parryActive      = parrySucceeds(m_parry);
    constexpr float kParryReach = 3.0f; // reflect bolts within this radius
    constexpr float kHitReach   = 0.6f; // bolt counts as a player hit within this radius

    auto& playerHealth = m_world.get<HealthComponent>(m_playerEntity);

    std::vector<entt::entity> toDestroy;
    auto view = m_world.view<Transform, ProjectileComponent>();
    for (auto [e, transform, proj] : view.each()) {
        // Only enemy-owned bolts target the player (the player's own bolts are
        // owned by m_playerBodyId and handled by ProjectileSystem vs. enemies).
        if (proj.ownerBodyId == m_playerBodyId)
            continue;

        float dist = glm::length(transform.position - playerPos);

        if (parryActive && dist <= kParryReach) {
            // Reflect: flip + speed-boost the velocity and re-own to the player
            // so the owner-ignore ray lets it strike enemies. Leave it alive.
            proj.velocity    = reflectProjectileVelocity(proj.velocity);
            proj.ownerBodyId = m_playerBodyId;
            // Successful parry payoff: dash refund, style, a flash + hitstop.
            m_player->refundDash(static_cast<int>(m_parryTuning.dashRefund));
            addStyleEvent(m_style, StyleEvent::Parry, m_styleConfig);
            m_parryFlash = 0.3f;
            triggerHitstop(m_hitstop, 0.05f);
            addTrauma(m_screenShake, 0.25f);
            m_audio.playAt(kSfxEnemyHit, transform.position);
            continue;
        }

        if (dist <= kHitReach) {
            // Bolt reaches the player: apply damage (gated by i-frames) and pop.
            if (applyDamage(playerHealth, m_playerIFrames, proj.damage, 0.5f)) {
                m_audio.play(kSfxPlayerHit);
                m_damageFlash = kDamageFlashTime;
                addTrauma(m_screenShake, 0.35f);
            }
            m_particles.emit(ParticleSystem::Effect::ImpactSparks, transform.position, {0.f, 1.f, 0.f}, 12);
            toDestroy.push_back(e);
        }
    }
    for (entt::entity e : toDestroy)
        if (m_world.valid(e))
            m_world.destroy(e);
}

void Engine::handleEnemyDeaths() {
    // Per-call LCG seed so gib scatter varies but needs no engine RNG state.
    uint32_t seed = static_cast<uint32_t>(m_timeAccum * 100003.f) | 1u;
    auto rnd      = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24); // [0,1)
    };

    auto view = m_world.view<EnemyComponent, Transform>();
    for (auto [entity, enemy, transform] : view.each()) {
        if (enemy.health > 0)
            continue;

        glm::vec3 center = transform.position;

        // Spawn a few gib boxes bursting outward; each is a dynamic physics body
        // whose mesh follows it until the despawn timer expires.
        const int kGibs = 5;
        for (int i = 0; i < kGibs; ++i) {
            glm::vec3 offset{(rnd() - 0.5f) * 0.4f, (rnd() - 0.5f) * 0.4f, (rnd() - 0.5f) * 0.4f};
            glm::vec3 vel{(rnd() - 0.5f) * 8.f, 3.f + rnd() * 5.f, (rnd() - 0.5f) * 8.f};
            glm::vec3 halfExtents{0.1f, 0.1f, 0.1f};
            uint32_t bodyId = m_physics.addDynamicBox(center + offset, halfExtents, vel);

            auto g = m_world.create();
            Transform t{};
            t.position = center + offset;
            m_world.emplace<Transform>(g, t);
            m_world.emplace<MeshComponent>(g, makeBoxMesh(*m_device, 0.1f, 0.1f, 0.1f, {0.7f, 0.1f, 0.1f}));
            m_world.emplace<MaterialComponent>(g, MaterialComponent{m_enemyAlbedo, m_enemySampler});
            m_world.emplace<GibComponent>(g, GibComponent{bodyId, 2.5f});
        }

        // Death VFX/audio + a meaty hitstop, and remove the capsule body (fixes
        // the orphaned-capsule leak that previously accumulated across waves).
        m_particles.emit(ParticleSystem::Effect::BloodBurst, center, {0.f, 1.f, 0.f}, 40);
        spawnTransientLight(center, {1.f, 0.2f, 0.1f}, 4.f, 3.f, 0.2f);
        triggerHitstop(m_hitstop, 0.05f);
        m_physics.removeBody(enemy.physicsBodyId);
        // Mark the body consumed so enemySystem's destroy doesn't double-remove
        // (enemySystem only calls world.destroy, never removeBody).
        enemy.physicsBodyId = UINT32_MAX;

        // Pickup drop (task 33): deterministic cadence — every 3rd kill drops an
        // orb, cycling kind so the player sees all three over a run. No RNG state.
        ++m_killCount;
        if (m_killCount % 3 == 0) {
            PickupComponent::Kind kind;
            int value;
            switch ((m_killCount / 3) % 3) {
            case 0:
                kind  = PickupComponent::Kind::Health;
                value = 25;
                break;
            case 1:
                kind  = PickupComponent::Kind::Ammo;
                value = 30;
                break;
            default:
                kind  = PickupComponent::Kind::DashCharge;
                value = 1;
                break;
            }
            // Drop a little above the floor at the death site so it is reachable.
            spawnPickup({center.x, 0.4f, center.z}, kind, value);
        }
    }
}

void Engine::updateGibs(float dt) {
    std::vector<entt::entity> expired;
    auto view = m_world.view<GibComponent, Transform>();
    for (auto [e, gib, transform] : view.each()) {
        transform.position = m_physics.getPosition(gib.physicsBodyId);
        gib.timer -= dt;
        if (gib.timer <= 0.f)
            expired.push_back(e);
    }
    for (entt::entity e : expired) {
        auto& gib = m_world.get<GibComponent>(e);
        m_physics.removeBody(gib.physicsBodyId);
        m_world.destroy(e);
    }
}

void Engine::spawnPickup(const glm::vec3& position, PickupComponent::Kind kind, int value) {
    // Color-code the orb by kind so it reads at a glance: green heal, gold ammo,
    // cyan dash. A small box mesh; pickupSystem spins it each frame.
    glm::vec3 color = kind == PickupComponent::Kind::Health ? glm::vec3{0.2f, 1.f, 0.3f}
                      : kind == PickupComponent::Kind::Ammo ? glm::vec3{1.f, 0.85f, 0.2f}
                                                            : glm::vec3{0.3f, 0.8f, 1.f};

    auto e = m_world.create();
    Transform t{};
    t.position = position;
    m_world.emplace<Transform>(e, t);
    m_world.emplace<MeshComponent>(e, makeBoxMesh(*m_device, 0.25f, 0.25f, 0.25f, color));
    m_world.emplace<MaterialComponent>(e, MaterialComponent{m_projectileAlbedo, m_projectileSampler});
    m_world.emplace<PickupComponent>(e, PickupComponent{kind, value});

    // A faint glow so orbs pop against the floor.
    spawnTransientLight(position + glm::vec3{0.f, 0.3f, 0.f}, color, 3.f, 2.f, 0.2f);
}

void Engine::pickupSystem(float dt) {
    const glm::vec3 playerPos     = m_camera.position;
    constexpr float kPickupRadius = 1.5f;

    auto& playerHealth = m_world.get<HealthComponent>(m_playerEntity);

    std::vector<entt::entity> collected;
    auto view = m_world.view<PickupComponent, Transform>();
    for (auto [e, pickup, transform] : view.each()) {
        // Spin the orb about Y for a little life, and bob it on a sine.
        transform.rotation = glm::angleAxis(m_timeAccum * 2.5f, glm::vec3{0.f, 1.f, 0.f});

        if (!withinPickupRange(playerPos, transform.position, kPickupRadius))
            continue;

        switch (pickup.kind) {
        case PickupComponent::Kind::Health: {
            int grant = pickupEffectMagnitude(pickup.value, playerHealth.max - playerHealth.current);
            playerHealth.current += grant;
            break;
        }
        case PickupComponent::Kind::Ammo: {
            WeaponComponent& w = currentWeapon();
            // Infinite-ammo weapons (ammo < 0) gain nothing; finite ones refill.
            if (w.ammo >= 0)
                w.ammo += pickup.value;
            break;
        }
        case PickupComponent::Kind::DashCharge:
            m_player->refundDash(pickup.value);
            break;
        }

        // Collect cue: green sparkle + a sound, then despawn (mesh auto-freed).
        m_audio.playAt(kSfxPickup, transform.position); // pickup cue (task 44)
        m_particles.emit(ParticleSystem::Effect::ImpactSparks, transform.position, {0.f, 1.f, 0.f}, 20);
        collected.push_back(e);
    }
    for (entt::entity e : collected)
        if (m_world.valid(e))
            m_world.destroy(e);

    (void)dt;
}

bool Engine::bossAlive() const {
    return !m_world.view<const BossComponent>().empty();
}

void Engine::spawnBoss() {
    if (bossAlive())
        return;

    // Spawn the boss at the far end of the arena, elevated so the large box
    // rests on the floor. A tall capsule body so the player's rays/projectiles
    // connect; AI + damage routing is driven by bossSystem (not enemySystem).
    glm::vec3 spawn{0.f, 2.5f, -8.f};
    constexpr float kHalfH = 1.6f, kRadius = 1.2f;
    uint32_t bodyId = m_physics.addCapsule(kHalfH, kRadius, spawn);

    auto e = m_world.create();
    Transform t{};
    t.position = spawn;
    m_world.emplace<Transform>(e, t);
    // Menacing dark-red box, clearly larger than a grunt.
    m_world.emplace<MeshComponent>(e, makeBoxMesh(*m_device, 1.2f, kHalfH + kRadius, 1.2f, {0.6f, 0.05f, 0.1f}));
    m_world.emplace<MaterialComponent>(e, MaterialComponent{m_enemyAlbedo, m_enemySampler});

    BossComponent boss{};
    boss.maxHealth     = 2000;
    boss.physicsBodyId = bodyId;
    boss.attackTimer   = 2.f; // brief grace before the first volley
    m_world.emplace<BossComponent>(e, boss);
    m_world.emplace<HealthComponent>(e, HealthComponent{boss.maxHealth, boss.maxHealth});

    // A persistent ominous light at the boss.
    spawnTransientLight(spawn, {1.f, 0.2f, 0.1f}, 8.f, 4.f, 1.5f);
    m_audio.playAt(kSfxEnemyDeath, spawn); // reuse as a roar cue
}

void Engine::bossSystem(float dt) {
    auto view = m_world.view<BossComponent, HealthComponent, Transform>();
    for (auto [e, boss, health, transform] : view.each()) {
        transform.position = m_physics.getPosition(boss.physicsBodyId);
        // Keep the boss roughly upright + grounded (capsule, no rotation control).

        // Death -> Victory. Free the body + entity (mesh auto-freed on_destroy).
        if (health.current <= 0) {
            m_particles.emit(ParticleSystem::Effect::Explosion, transform.position, {0.f, 1.f, 0.f}, 96);
            spawnTransientLight(transform.position, {1.f, 0.5f, 0.2f}, 12.f, 8.f, 0.6f);
            triggerHitstop(m_hitstop, 0.15f);
            addTrauma(m_screenShake, 0.8f);
            m_physics.removeBody(boss.physicsBodyId);
            m_world.destroy(e);
            m_state = GameState::Victory;
            recordHighScore();
            m_scripts.onPlayerDeath(m_wave.score); // reuse as a run-end hook
            return;
        }

        // Phase transition: when the computed phase exceeds the stored one, open
        // a brief parryable vulnerable window and bump the attack cadence.
        std::span<const float> thresholds{boss.phaseHealthThresholds, 3};
        int newPhase = bossPhaseForHealth(health.current, boss.maxHealth, thresholds);
        if (newPhase > boss.phase) {
            boss.phase           = newPhase;
            boss.vulnerableTimer = 2.0f; // window the player can punish / parry
            boss.attackTimer     = 1.0f;
            m_audio.playAt(kSfxEnemyHit, transform.position);
            spawnTransientLight(transform.position, {1.f, 0.9f, 0.3f}, 8.f, 6.f, 0.4f);
            addTrauma(m_screenShake, 0.4f);
        }

        if (boss.vulnerableTimer > 0.f) {
            // During the vulnerable window the boss telegraphs (holds fire) and
            // glows; the player can deal extra damage / parry incoming nothing.
            boss.vulnerableTimer -= dt;
            continue;
        }

        // Attack loop: count down, then fire a telegraphed pattern at the player.
        boss.attackTimer -= dt;
        if (boss.attackTimer > 0.f)
            continue;

        glm::vec3 toPlayer = m_camera.position - transform.position;
        float dist         = glm::length(toPlayer);
        glm::vec3 dir      = dist > 1e-4f ? toPlayer / dist : glm::vec3{0.f, 0.f, 1.f};
        glm::vec3 muzzle   = transform.position + dir * 1.6f + glm::vec3{0.f, 0.5f, 0.f};

        // Pattern escalates with the phase: a wider, faster volley each phase.
        // Even patterns = a fan volley, odd = a faster straight burst (charge).
        const int phase   = boss.phase;
        const int pellets = 3 + phase * 2; // 3,5,7,...
        const float speed = 14.f + static_cast<float>(phase) * 3.f;
        const int damage  = 12 + phase * 4;

        if ((boss.pattern & 1u) == 0u) {
            // Fan volley: spread pellets in a horizontal arc toward the player.
            glm::vec3 right     = glm::normalize(glm::cross(dir, glm::vec3{0.f, 1.f, 0.f}));
            const float halfArc = 0.35f + static_cast<float>(phase) * 0.1f;
            for (int i = 0; i < pellets; ++i) {
                float t     = pellets > 1 ? (static_cast<float>(i) / static_cast<float>(pellets - 1)) * 2.f - 1.f : 0.f;
                glm::vec3 d = glm::normalize(dir + right * (t * halfArc));
                spawnEnemyProjectile(muzzle, d * speed, damage, boss.physicsBodyId);
            }
        } else {
            // Charge burst: a tight, fast straight stream the player must dodge.
            for (int i = 0; i < pellets; ++i)
                spawnEnemyProjectile(muzzle + dir * (static_cast<float>(i) * 0.4f), dir * (speed * 1.4f), damage,
                                     boss.physicsBodyId);
        }

        m_audio.playAt(kSfxWeaponFire, transform.position);
        ++boss.pattern;
        // Faster cadence in later phases.
        boss.attackTimer = 2.2f - static_cast<float>(phase) * 0.4f;
        if (boss.attackTimer < 0.6f)
            boss.attackTimer = 0.6f;
    }
}

void Engine::clearPhysicsActors() {
    // Remove enemy capsules.
    auto enemies = m_world.view<EnemyComponent>();
    for (auto [e, enemy] : enemies.each()) {
        if (enemy.physicsBodyId != UINT32_MAX)
            m_physics.removeBody(enemy.physicsBodyId);
    }
    // Remove the boss body (task 40) if one is alive.
    auto bosses = m_world.view<BossComponent>();
    for (auto [e, boss] : bosses.each())
        m_physics.removeBody(boss.physicsBodyId);
    m_world.destroy(bosses.begin(), bosses.end());
    // Remove gib bodies + entities.
    auto gibs = m_world.view<GibComponent>();
    for (auto [e, gib] : gibs.each())
        m_physics.removeBody(gib.physicsBodyId);
    m_world.destroy(gibs.begin(), gibs.end());
}

WeaponComponent Engine::effectiveWeapon() const {
    const WeaponComponent& base = m_weapons[m_weaponIndex];
    if (m_weaponIndex < m_weaponMods.size())
        return withMods(base, m_weaponMods[m_weaponIndex]);
    return base;
}

AltFireMode Engine::altFireFor(WeaponType type) const {
    switch (type) {
    case WeaponType::Hitscan:
        return AltFireMode::ShotgunSpread;
    case WeaponType::Rocket:
        return AltFireMode::ChargedRocket;
    case WeaponType::Plasma:
        return AltFireMode::PlasmaOverheat;
    }
    return AltFireMode::None;
}

void Engine::grantWeaponUpgrade() {
    if (m_weapons.empty())
        return;
    m_weaponUpgrades.resize(m_weapons.size());
    m_weaponMods.resize(m_weapons.size());

    // Grant a deterministic upgrade to the active weapon, cycling stat kinds by
    // wave so repeated intermissions build a varied modifier stack.
    WeaponUpgrade up{};
    switch (m_wave.wave % 3) {
    case 0:
        up = WeaponUpgrade{WeaponUpgradeKind::Damage, 1.25f};
        break;
    case 1:
        up = WeaponUpgrade{WeaponUpgradeKind::FireRate, 1.15f};
        break;
    default:
        up = WeaponUpgrade{WeaponUpgradeKind::ProjectileSpeed, 1.2f};
        break;
    }
    size_t slot = m_weaponIndex;
    m_weaponUpgrades[slot].push_back(up);
    applyUpgrade(m_weaponMods[slot], up);
}

entt::entity Engine::spawnEnemy(glm::vec3 position, rhi::RHITexture albedo, rhi::RHISampler sampler,
                                EnemyArchetype archetype) {
    constexpr float kHalfH = 0.6f, kRadius = 0.3f;
    uint32_t bodyId = m_physics.addCapsule(kHalfH, kRadius, position);

    // Color the box per archetype so the three families read at a glance:
    // Grunt red, Charger orange, Ranged blue.
    glm::vec3 color = archetype == EnemyArchetype::Charger  ? glm::vec3{1.f, 0.5f, 0.1f}
                      : archetype == EnemyArchetype::Ranged ? glm::vec3{0.3f, 0.5f, 1.f}
                                                            : glm::vec3{1.f, 0.2f, 0.2f};

    auto e = m_world.create();
    Transform t{};
    t.position = position;
    m_world.emplace<Transform>(e, t);
    m_world.emplace<MeshComponent>(e, makeBoxMesh(*m_device, 0.3f, kHalfH + kRadius, 0.3f, color));
    m_world.emplace<MaterialComponent>(e, MaterialComponent{albedo, sampler});
    EnemyComponent ec{};
    // Stamp the archetype's stat block first (overwrites only stat fields), then
    // assign the runtime-only physics body id so it survives.
    applyArchetype(ec, archetype);
    ec.physicsBodyId = bodyId;
    // Data-driven enemy tuning from the Lua config overrides the archetype block
    // for the basic Grunt (the script tunes the default family only).
    if (m_enemyStats.overrode && archetype == EnemyArchetype::Grunt) {
        ec.health       = m_enemyStats.health;
        ec.moveSpeed    = m_enemyStats.speed;
        ec.attackDamage = m_enemyStats.damage;
    }
    m_world.emplace<EnemyComponent>(e, ec);
    return e;
}

EnemyArchetype Engine::archetypeForWave(int wave, int spawnIndex) const {
    // Wave 1: all Grunts. From wave 2 mix in Chargers; from wave 3 add Ranged.
    // Deterministic from (wave, spawnIndex) so spawns are reproducible.
    if (wave <= 1)
        return EnemyArchetype::Grunt;
    int sel = (wave + spawnIndex) % 3;
    if (wave >= 3 && sel == 2)
        return EnemyArchetype::Ranged;
    if (sel == 1)
        return EnemyArchetype::Charger;
    return EnemyArchetype::Grunt;
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

bool Engine::loadLevel(const char* relPath, rhi::RHITexture albedo, rhi::RHISampler sampler, glm::vec3* playerStart) {
    std::filesystem::path full = paths::assets() / relPath;
    return LevelLoader::load(full, m_world, m_physics, *m_device, albedo, sampler, playerStart);
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
                // Settings open (over Menu): ESC closes the panel back to the
                // menu. In Playing: ESC pauses to the Menu. Anywhere else: quit.
                if (m_settingsOpen)
                    m_settingsOpen = false;
                else if (m_state == GameState::Playing)
                    m_state = GameState::Menu;
                else
                    m_running = false;
            }
            // S toggles the settings sub-screen on any non-playing screen.
            if (e.key.key == SDLK_S && !e.key.repeat && m_state != GameState::Playing) {
                m_settingsOpen = !m_settingsOpen;
                m_activeSlider = -1;
                m_audio.playUI(kSfxUiClick);
            }
            // Enter (re)starts a run from any non-playing screen (not while the
            // settings panel is open, where Enter would otherwise jump in-game).
            if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && m_state != GameState::Playing &&
                !m_settingsOpen)
                startGame();
            // Number keys 1..N select a weapon slot directly.
            if (e.key.key >= SDLK_1 && e.key.key <= SDLK_9) {
                size_t slot = static_cast<size_t>(e.key.key - SDLK_1);
                if (slot < m_weapons.size())
                    m_weaponIndex = slot;
            }
            // Parry on the F key (distinct from LMB fire / RMB alt-fire). Edge
            // triggered so a held key opens one window, not one per frame.
            if (e.key.key == SDLK_F && !e.key.repeat && m_state == GameState::Playing)
                m_parryPressed = true;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            // While the settings panel is open the cursor is absolute: route a
            // held drag onto the active slider; otherwise the FPS look only
            // applies in relative (Playing) mode.
            if (m_settingsOpen) {
                if (m_activeSlider >= 0)
                    handleSettingsInput(e.motion.x, e.motion.y, false);
            } else if (m_state == GameState::Playing) {
                m_camera.rotate(e.motion.xrel, e.motion.yrel);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (m_settingsOpen) {
                    // Route the click onto the settings widgets; a click on empty
                    // space is ignored (does not start the game).
                    handleSettingsInput(e.button.x, e.button.y, true);
                } else if (m_state != GameState::Playing) {
                    // A click in the bottom-band "SETTINGS" hint opens the panel;
                    // anywhere else (re)starts the run. The hint rect mirrors the
                    // text position in renderStateOverlay.
                    const float h          = static_cast<float>(m_windowHeight);
                    const float hintTop    = h * 0.70f + 8.f * 3.f + 16.f;
                    const bool hitSettings = e.button.y >= hintTop - 8.f && e.button.y <= hintTop + 8.f * 2.f + 8.f;
                    if (hitSettings) {
                        m_settingsOpen = true;
                        m_activeSlider = -1;
                        m_audio.playUI(kSfxUiClick);
                    } else {
                        startGame();
                    }
                } else {
                    m_firePressed = true;
                }
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                // Right mouse = alt-fire (shotgun spread / charged / overheat).
                if (m_state == GameState::Playing)
                    m_altFirePressed = true;
            } else if (e.button.button == SDL_BUTTON_MIDDLE) {
                // Middle mouse also parries (mirrors the F key).
                if (m_state == GameState::Playing)
                    m_parryPressed = true;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT)
                m_activeSlider = -1; // end any slider drag
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
    float realDt = static_cast<float>(now - m_lastTick) / static_cast<float>(SDL_GetPerformanceFrequency());
    realDt       = std::min(realDt, 0.05f);
    m_lastTick   = now;

    // Hitstop (task 25): tick on the real (unscaled) dt and scale sim dt by the
    // returned freeze factor. m_timeAccum tracks REAL time so screenshake noise
    // and HUD pulses keep advancing even while the sim is frozen.
    float hitstopScale = tickHitstop(m_hitstop, realDt);
    m_dt               = realDt * hitstopScale;

    m_timeAccum += realDt;
    if (m_damageFlash > 0.f)
        m_damageFlash -= realDt;
    if (m_parryFlash > 0.f)
        m_parryFlash -= realDt;

#ifdef DS_DEV
    // Shader hot-reload (task 22): poll the .slang sources periodically (not
    // every frame — slangc + a stat per source is not free) and reissue the
    // affected pipeline(s) for any shader that recompiled. No-op in shipping.
    if (++m_frameCounter % kShaderPollFrames == 0) {
        for (const std::string& name : m_shaderWatcher.poll())
            reloadShader(name);
    }
#endif

    // Mouse mode (task 44): relative (hidden cursor, FPS look) only while
    // actively playing; absolute (visible cursor) in menus / settings / pause so
    // the cursor can hit the settings widgets.
    setRelativeMouse(m_state == GameState::Playing && !m_settingsOpen);

    // Music ducking (task 44): drop the track on the YOU DIED / Victory screens
    // for impact, full volume everywhere else. Cheap to set every frame (the
    // bus volume just gets reassigned).
    m_audio.duckMusic((m_state == GameState::Dead || m_state == GameState::Victory) ? 0.35f : 1.f);

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

    // --- Parry (task 35): tick timers, then open a window on the parry input
    // (self-gated on cooldown inside triggerParry).
    tickParry(m_parry, m_dt);
    if (m_parryPressed) {
        triggerParry(m_parry, m_parryTuning);
        m_audio.play(kSfxParry); // parry chime (task 44)
    }
    m_parryPressed = false;

    // --- Style: track a recent weapon switch so a kill right after swapping
    // scores the WeaponSwitchKill bonus.
    if (m_weaponIndex != m_prevWeaponIndex) {
        m_weaponSwitchedRecently = true;
        m_weaponSwitchTimer      = 1.0f;
        m_prevWeaponIndex        = m_weaponIndex;
    }
    if (m_weaponSwitchTimer > 0.f) {
        m_weaponSwitchTimer -= m_dt;
        if (m_weaponSwitchTimer <= 0.f)
            m_weaponSwitchedRecently = false;
    }
    // Capture pre-step movement context for kill-style classification.
    const bool playerAirborne = m_camera.position.y > kPlayerSpawn.y + 0.3f;
    const bool playerDashing  = m_player->dashedThisFrame();

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

    // Footsteps (task 44): tick a cadence while grounded and moving; dashing /
    // sliding have their own cues so they suppress steps. "Grounded" is
    // approximated by the player being near the spawn eye height (the same
    // airborne heuristic the style meter uses) since PlayerController exposes no
    // grounded flag.
    {
        const bool grounded = m_camera.position.y <= kPlayerSpawn.y + 0.3f;
        const bool moving   = glm::length(moveDir) > 0.f;
        if (grounded && moving && !m_player->sliding() && !m_player->dashedThisFrame()) {
            m_footstepTimer -= m_dt;
            if (m_footstepTimer <= 0.f) {
                m_audio.play(kSfxFootstep);
                m_footstepTimer = kFootstepInterval;
            }
        } else {
            m_footstepTimer = 0.f; // first step on resuming movement is immediate
        }
    }

    auto& playerHealth   = m_world.get<HealthComponent>(m_playerEntity);
    int healthBeforeStep = playerHealth.current;

    // Parry damage negation (task 35): while the parry window is active, gate ALL
    // incoming contact damage this frame by raising the i-frame timer past the
    // step (applyDamage drops any hit while iFrames > 0). A successful parry that
    // actually had something to react to refunds a dash charge + scores style;
    // here we always treat an active window as "succeeded" for negation.
    const bool parryActive = parrySucceeds(m_parry);
    if (parryActive)
        m_playerIFrames = std::max(m_playerIFrames, m_dt + 1e-3f);

    // Ragdoll/gibs (task 36): spawn debris + free the capsule for any enemy that
    // died (health<=0) before enemySystem destroys the entity this frame.
    handleEnemyDeaths();

    // enemySystem destroys enemies whose health has dropped to <=0 this frame;
    // diffing the live count is how we detect kills for score/combo + waves.
    int enemiesBeforeStep = static_cast<int>(m_world.view<EnemyComponent>().size());
    enemySystem(
        m_world, m_physics, m_camera.position, m_dt, &playerHealth, &m_playerIFrames,
        [this](const EnemyProjectileSpawn& s) { spawnEnemyProjectile(s.origin, s.velocity, s.damage, s.ownerBodyId); });
    int enemiesAfterStep = static_cast<int>(m_world.view<EnemyComponent>().size());
    if (playerHealth.current < healthBeforeStep) {
        m_audio.play(kSfxPlayerHit);
        m_damageFlash = kDamageFlashTime;
        // Getting hit shakes the screen.
        addTrauma(m_screenShake, 0.45f);
    }

    // Register each kill from this frame (advances score, combo and best combo).
    // EnemySystem destroys the bodies internally, so per-kill position is not
    // available here; the script callback gets the player position as the
    // best-known impact site.
    int killsThisFrame = enemiesBeforeStep - enemiesAfterStep;
    for (int i = enemiesAfterStep; i < enemiesBeforeStep; ++i) {
        registerKill(m_wave, m_waveConfig);
        m_scripts.onEnemyDeath(0, m_camera.position.x, m_camera.position.y, m_camera.position.z);
    }
    // Style meter (task 32): score the flashiest applicable category per kill.
    // Multi-kill (>=2 this frame) trumps the single-kill variants.
    if (killsThisFrame > 0) {
        StyleEvent ev = StyleEvent::Kill;
        if (killsThisFrame >= 2)
            ev = StyleEvent::MultiKill;
        else if (playerDashing)
            ev = StyleEvent::DashKill;
        else if (playerAirborne)
            ev = StyleEvent::AirKill;
        else if (m_weaponSwitchedRecently)
            ev = StyleEvent::WeaponSwitchKill;
        for (int i = 0; i < killsThisFrame; ++i)
            addStyleEvent(m_style, ev, m_styleConfig);
        // Rank-up sting (task 44): play a UI sting when a kill pushes the style
        // rank above its previous value. Only on increases (decay-down is silent).
        if (m_style.rank > m_prevRank)
            m_audio.playUI(kSfxRankUp);
        m_prevRank = m_style.rank;
        // A kill punches a little hitstop + trauma for crunch.
        triggerHitstop(m_hitstop, 0.04f);
        addTrauma(m_screenShake, 0.2f);
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
    // Effective (upgrade-modded) weapon drives damage + fire rate (task 37).
    WeaponComponent eff = effectiveWeapon();
    const bool wantFire = m_firePressed || m_altFirePressed;
    if (wantFire && weapon.cooldown <= 0.f && weapon.ammo != 0) {
        m_audio.play(kSfxWeaponFire);
        // Muzzle flash at the gun, biased along the look direction.
        glm::vec3 front  = glm::normalize(m_camera.front());
        glm::vec3 muzzle = m_camera.position + front * 0.4f;
        m_particles.emit(ParticleSystem::Effect::MuzzleFlash, muzzle, front, 12);
        // Brief warm muzzle-flash light (~2 frames) to pop nearby geometry.
        spawnTransientLight(muzzle, {1.f, 0.85f, 0.5f}, 6.f, 4.f, 0.05f);

        if (m_altFirePressed)
            fireWeaponAlt(eff);
        else
            fireWeaponPrimary(eff);

        // Recoil kick (upward + slight horizontal) and a touch of trauma.
        float kick = m_altFirePressed ? 0.05f : 0.02f;
        addRecoil(m_recoil, glm::vec2{0.f, kick});
        addTrauma(m_screenShake, m_altFirePressed ? 0.18f : 0.08f);

        float rate            = eff.fireRate > 0.f ? eff.fireRate : weapon.fireRate;
        weapon.cooldown       = 1.f / rate;
        weapon.firedThisFrame = true;
        if (weapon.ammo > 0)
            --weapon.ammo;
    }
    m_firePressed    = false;
    m_altFirePressed = false;

    // Resolve enemy projectiles against the player first (parry reflect + player
    // damage) so reflected bolts re-owned to the player are then advanced and
    // collided against enemies by ProjectileSystem below.
    processEnemyProjectiles();

    // Advance in-flight projectiles: integrate, resolve hits + splash, and spawn
    // explosion VFX/light on detonation. Particles/lights always exist here, so
    // the impact callback is safe to wire unconditionally.
    projectileSystem(m_world, m_physics, m_dt, [this](const ProjectileImpact& hit) {
        if (hit.splashRadius > 0.f) {
            // Rocket: fireball + bright transient light scaled to the blast.
            m_particles.emit(ParticleSystem::Effect::Explosion, hit.position, hit.normal, 48);
            spawnTransientLight(hit.position, {1.f, 0.6f, 0.25f}, hit.splashRadius * 1.5f, 6.f, 0.25f);
            m_audio.playAt(kSfxExplosion, hit.position); // explosion SFX (task 44)
            triggerHitstop(m_hitstop, 0.05f);
            addTrauma(m_screenShake, 0.3f);
        } else {
            // Plasma / non-splash: small spark pop.
            m_particles.emit(ParticleSystem::Effect::ImpactSparks, hit.position, hit.normal, 16);
            spawnTransientLight(hit.position, {0.4f, 1.f, 0.6f}, 3.f, 3.f, 0.1f);
        }
        if (hit.hitEnemy)
            m_audio.playAt(kSfxEnemyHit, hit.position);
        // Boss direct hit (task 40): ProjectileSystem only damages EnemyComponent,
        // so route a direct hit on the boss's body to its HealthComponent here.
        if (hit.directDamage > 0 && hit.hitBodyId != UINT32_MAX) {
            auto bossView = m_world.view<BossComponent, HealthComponent, Transform>();
            for (auto [entity, boss, health, btf] : bossView.each()) {
                if (boss.physicsBodyId != hit.hitBodyId)
                    continue;
                int dmg = boss.vulnerableTimer > 0.f ? hit.directDamage * 2 : hit.directDamage;
                health.current -= dmg;
                m_audio.playAt(kSfxEnemyHit, hit.position);
                addDamageEvent(m_damageEvents, hit.position, dmg, health.current <= 0);
                triggerHitMarker(m_hitMarker, health.current <= 0);
                break;
            }
        }
        // Floating damage numbers + hit marker for every enemy this blast hit.
        for (const ProjectileEnemyHit& eh : hit.enemyHits) {
            addDamageEvent(m_damageEvents, eh.worldPos, eh.amount, eh.killed);
            triggerHitMarker(m_hitMarker, eh.killed);
        }
    });

    // Tick game-feel + style + combat-feedback timers (task 25/29/32).
    decayTrauma(m_screenShake, m_dt);
    tickRecoil(m_recoil, m_dt);
    tickStyle(m_style, m_dt, m_styleConfig);
    // Track the post-decay rank so a future climb past it re-triggers the sting.
    if (m_style.rank < m_prevRank)
        m_prevRank = m_style.rank;
    tickDamageEvents(m_damageEvents, m_dt);
    tickHitMarker(m_hitMarker, m_dt);

    // Sync + age gib debris (task 36); expired chunks free their physics body.
    updateGibs(m_dt);

    // Pickups (task 33): spin orbs + collect any the player walked into. Boss
    // (task 40): drive its phased AI; on its death this sets Victory.
    pickupSystem(m_dt);
    bossSystem(m_dt);
    // bossSystem may have ended the run on the killing blow.
    if (m_state != GameState::Playing)
        return;

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
        // elapse via tickWave on subsequent frames. Grant a weapon upgrade once
        // per cleared wave (task 37).
        m_wave.intermission      = m_waveConfig.intermissionTime;
        m_wave.intermissionArmed = true;
        if (m_wave.wave != m_lastUpgradeWave) {
            grantWeaponUpgrade();
            m_lastUpgradeWave = m_wave.wave;
        }
        return;
    }

    // Intermission has elapsed: advance to the next wave (or trigger the boss).
    m_wave.intermissionArmed = false;
    advanceWave(m_wave, m_waveConfig);
    if (m_wave.cleared) {
        // Final wave cleared: instead of an immediate Victory, spawn ONE boss as
        // the climactic encounter (task 40). bossSystem flips the state to
        // Victory only once the boss dies. m_wave.cleared stays true so this
        // function early-returns each frame while the boss is alive.
        spawnBoss();
        m_scripts.onWaveStart(m_wave.wave);
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

void Engine::renderDepthOnly(rhi::IRHICommandList* cmd, const glm::mat4& lightSpace) {
    cmd->setPipeline(m_shadowPipeline);

    // The shadow vertex shader assembles model from the instanced stream and
    // multiplies by lightSpace (the only vertex uniform), so push lightSpace
    // once (task 28). Depth-only: material/albedo is irrelevant, so entities are
    // grouped by mesh buffers alone (albedo == nullptr in the key) and drawn as
    // one instanced drawIndexed per distinct geometry. The model matrices are
    // uploaded to the SAME shared instance buffer renderSystem uses; the data is
    // identical, so the scene pass's later re-upload is harmless.
    cmd->pushVertexConstants(&lightSpace, sizeof(lightSpace));

    std::vector<InstanceDraw> draws;
    std::unordered_map<InstanceKey, MeshComponent, InstanceKeyHash> batchMesh;
    auto view = m_world.view<Transform, MeshComponent>();
    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& mesh      = view.get<MeshComponent>(entity);

        InstanceKey key{mesh.vertexBuffer.ptr, mesh.indexBuffer.ptr, nullptr};
        InstanceDraw d;
        d.key        = key;
        d.indexCount = mesh.indexCount;
        d.indexType  = mesh.indexType;
        d.model      = transform.modelMatrix();
        draws.push_back(d);
        batchMesh.try_emplace(key, mesh);
    }

    std::vector<DrawBatch> batches = buildBatches(draws);

    // Concatenate model matrices (batch order), clamp to capacity, upload once.
    std::vector<glm::mat4> instanceData;
    std::vector<uint32_t> firstInstance(batches.size(), 0);
    std::vector<uint32_t> instanceCount(batches.size(), 0);
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        firstInstance[bi] = static_cast<uint32_t>(instanceData.size());
        for (const glm::mat4& model : batches[bi].models) {
            if (static_cast<uint32_t>(instanceData.size()) >= kMaxInstances)
                break;
            instanceData.push_back(model);
        }
        instanceCount[bi] = static_cast<uint32_t>(instanceData.size()) - firstInstance[bi];
    }

    if (instanceData.empty())
        return;
    m_device->uploadImmediate(m_shadowInstanceBuffer, instanceData.data(), instanceData.size() * sizeof(glm::mat4), 0);

    constexpr uint64_t kMat4Size = sizeof(glm::mat4);
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        if (instanceCount[bi] == 0)
            continue;
        const MeshComponent& mesh = batchMesh[batches[bi].key];
        cmd->setVertexBuffer(0, mesh.vertexBuffer);
        cmd->setVertexBuffer(1, m_shadowInstanceBuffer, firstInstance[bi] * kMat4Size);
        cmd->setIndexBuffer(mesh.indexBuffer, mesh.indexType);
        cmd->drawIndexed(batches[bi].indexCount, instanceCount[bi], 0, 0, 0);
    }
}

void Engine::renderBloom(rhi::IRHICommandList* cmd) {
    // Bright-pass + separable Gaussian blur (task 26). Each stage is its own
    // render pass into an RGBA16Float bloom target with a fullscreen draw(3).
    // Leaves the final blurred bloom in m_bloomA for the tonemap composite.
    // Texel size tracks the bloom RT dimensions (half- or full-window res per
    // the quality tier, task 34), not the window, so the blur radius is correct.
    const float texelW = 1.f / static_cast<float>(m_bloomWidth);
    const float texelH = 1.f / static_cast<float>(m_bloomHeight);

    auto fullscreenPass = [&](rhi::RHITexture target, rhi::RHIPipeline pipeline, rhi::RHITexture source,
                              const void* constants, uint32_t constantsSize) {
        rhi::ColorAttachment color{};
        color.texture = target;
        color.loadOp  = rhi::LoadOp::Clear;
        color.storeOp = rhi::StoreOp::Store;

        rhi::RenderPassDesc pass{};
        pass.colorAttachments = {&color, 1};
        pass.depthAttachment  = nullptr;

        cmd->beginRenderPass(pass);
        cmd->setPipeline(pipeline);
        cmd->bindFragmentTexture(0, source, m_linearSampler);
        cmd->pushFragmentConstants(constants, constantsSize);
        cmd->draw(3, 1, 0, 0);
        cmd->endRenderPass();
    };

    // Bright-pass: HDR -> bloomA.
    struct BrightConstants {
        float threshold;
        float knee;
        float pad[2] = {0.f, 0.f};
    } bright{kBloomThreshold, kBloomKnee, {0.f, 0.f}};
    fullscreenPass(m_bloomA, m_brightPipeline, m_hdrTexture, &bright, sizeof(bright));

    // Horizontal blur: bloomA -> bloomB.
    struct BlurConstants {
        float dir[2];
        float pad[2] = {0.f, 0.f};
    };
    BlurConstants horizontal{{texelW, 0.f}, {0.f, 0.f}};
    fullscreenPass(m_bloomB, m_blurPipeline, m_bloomA, &horizontal, sizeof(horizontal));

    // Vertical blur: bloomB -> bloomA (final bloom result).
    BlurConstants vertical{{0.f, texelH}, {0.f, 0.f}};
    fullscreenPass(m_bloomA, m_blurPipeline, m_bloomB, &vertical, sizeof(vertical));
}

void Engine::render() {
    DS_ZONE();
    rhi::IRHICommandList* cmd = m_device->beginFrame();
    if (!cmd)
        return;

    float aspect = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);

    // Game feel (task 25): perturb a copy of the camera by the recoil spring
    // offset and trauma-driven screenshake before building the view matrix, so
    // the underlying aim state (yaw/pitch) is never corrupted. shakeOffset is in
    // radians; recoil offset is (yaw, pitch) radians; convert to the camera's
    // degree-based angles. m_timeAccum is monotonic real time for the noise.
    Camera shakeCam = m_camera;
    glm::vec3 shake = shakeOffset(m_screenShake.trauma, m_timeAccum);
    shakeCam.yaw += glm::degrees(m_recoil.offset.x + shake.y);
    shakeCam.pitch += glm::degrees(m_recoil.offset.y + shake.x);
    glm::mat4 viewProj = shakeCam.projMatrix(aspect) * shakeCam.viewMatrix();

    // Light-space matrix for the sun shadow map, rebuilt each frame. Static
    // bounds covering the 20x5x20 arena (walls at +-10 in X/Z, floor at y=0,
    // ceiling ~5) with generous slack so tall casters / spawned geometry stay
    // inside the ortho frustum. kSunDir is the *toward-the-sun* vector (the L
    // used in mesh.slang's BRDF, pointing up at +Y); sunLightSpaceMatrix wants
    // the direction the light TRAVELS, so we negate it to place the shadow
    // camera above the scene looking down.
    const ds::Bounds arenaBounds{glm::vec3{-12.f, -2.f, -12.f}, glm::vec3{12.f, 8.f, 12.f}};
    m_sunLightSpace = ds::sunLightSpaceMatrix(-kSunDir, arenaBounds);

    // Build the frame as a declarative pass list (task 23). The shadow and
    // scene passes are added to the graph; bloom still manages its own internal
    // passes and runs as a plain call between graph executions (see below); the
    // tonemap+HUD pass is then added and the graph is executed in two phases so
    // the emitted beginRenderPass/endRenderPass sequence is byte-for-byte the
    // same as the previous imperative code: shadow, scene, [bloom x3], tonemap.
    m_renderGraph.clear();

    // --- Shadow pass: scene depth from the sun's POV -> m_shadowMap. --------
    // On the Minimum tier (task 34) the pass still runs but ONLY clears the
    // shadow map to far depth (1.0); it draws no geometry, so every later
    // sampleSunShadow lookup reads 1.0 and the sun term is fully lit. This keeps
    // the mesh FS binding (texture slot 1) valid without a separate code path,
    // while skipping the per-object depth draws that dominate the pass cost.
    {
        rhi::DepthAttachment depth{};
        depth.texture    = m_shadowMap;
        depth.loadOp     = rhi::LoadOp::Clear;
        depth.storeOp    = rhi::StoreOp::Store;
        depth.clearDepth = 1.0f;

        RenderPass shadowPass;
        shadowPass.name            = "shadow";
        shadowPass.depthAttachment = depth; // depth-only: no color attachments
        glm::mat4 lightSpace       = m_sunLightSpace;
        const bool drawShadows     = m_quality.shadows;
        shadowPass.record          = [this, lightSpace, drawShadows](rhi::IRHICommandList& c) {
            if (drawShadows)
                renderDepthOnly(&c, lightSpace);
        };
        m_renderGraph.addPass(std::move(shadowPass));
    }

    // --- Pass A: scene + particles -> offscreen HDR target. ----------------
    {
        rhi::ColorAttachment color{};
        color.texture       = m_hdrTexture;
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

        RenderPass scenePass;
        scenePass.name             = "scene";
        scenePass.colorAttachments = {color};
        scenePass.depthAttachment  = depth;
        scenePass.record           = [this, viewProj](rhi::IRHICommandList& c) {
            rhi::IRHICommandList* cmd = &c;
            // Bind the shadow map (fragment texture slot 1) and push the
            // light-space matrix (fragment uniform slot 2) once; both are
            // constant for the pass and renderSystem only touches texture slot
            // 0 + uniform slots 0/1.
            cmd->setPipeline(m_meshPipeline);
            cmd->bindFragmentTexture(1, m_shadowMap, m_shadowSampler);
            struct ShadowData {
                glm::mat4 lightSpace;
                int32_t res;
                int32_t pad0 = 0;
                int32_t pad1 = 0;
                int32_t pad2 = 0;
            } shadowData{m_sunLightSpace, static_cast<int32_t>(m_shadowMapSize), 0, 0, 0};
            cmd->pushFragmentConstants(&shadowData, sizeof(shadowData), 2);

            // Always draw the 3D scene so the world is visible behind paused
            // overlays (Dead / Victory) and so the Menu has a backdrop. The
            // frustum (task 24) is extracted from the same viewProj used to draw
            // so dynamic actors outside the view are skipped.
            Frustum frustum = extractFrustum(viewProj);
            RenderContext ctx{cmd, m_meshPipeline, viewProj, m_camera.position, &m_lightBuffer, &frustum};
            ctx.device           = m_device.get();
            ctx.instanceBuffer   = m_instanceBuffer;
            ctx.instanceCapacity = kMaxInstances;
            renderSystem(m_world, ctx);

            renderParticles(cmd, viewProj);
        };
        m_renderGraph.addPass(std::move(scenePass));
    }

    // GPU particle sim (task 39, Enhanced tier): run the compute dispatch that
    // fills the instance buffer BEFORE any render pass opens (SDL3 forbids a
    // compute pass nested in a render pass). No-op on the Minimum tier, where
    // renderParticles() uploads the CPU-built instances inside the scene pass.
    dispatchParticleCompute(cmd);

    // Execute the shadow + scene passes. Bloom opens its OWN render passes
    // internally, so it must NOT be wrapped as a graph pass (that would
    // double-open). It runs as a plain call here, exactly between the scene and
    // tonemap passes, preserving the original begin/end ordering.
    m_renderGraph.execute(*cmd);

    // --- Bloom: bright-pass(HDR)->bloomA, blurH->bloomB, blurV->bloomA. -----
    renderBloom(cmd);

    // --- Pass B: tonemap HDR -> swapchain, then LDR HUD/overlay on top. -----
    m_renderGraph.clear();
    {
        rhi::ColorAttachment color{};
        // Invalid texture handle => the backend binds the swapchain image.
        color.loadOp  = rhi::LoadOp::Clear;
        color.storeOp = rhi::StoreOp::Store;

        RenderPass tonemapPass;
        tonemapPass.name             = "tonemap";
        tonemapPass.colorAttachments = {color};
        tonemapPass.record           = [this](rhi::IRHICommandList& c) {
            rhi::IRHICommandList* cmd = &c;
            // Fullscreen tonemap: add bloom to the HDR target, then exposure + ACES.
            struct ExposureConstants {
                float exposure       = 1.0f;
                float bloomIntensity = kBloomIntensity;
                float pad[2]         = {0.f, 0.f};
            } exposure;
            cmd->setPipeline(m_tonemapPipeline);
            cmd->bindFragmentTexture(0, m_hdrTexture, m_linearSampler);
            cmd->bindFragmentTexture(1, m_bloomA, m_linearSampler);
            cmd->pushFragmentConstants(&exposure, sizeof(exposure));
            cmd->draw(3, 1, 0, 0);

            // The combat HUD (health/ammo/crosshair) only makes sense while
            // playing; the other states draw their own overlay instead. Both
            // draw LDR UI quads into this same swapchain pass so the HUD stays
            // crisp.
            if (m_state == GameState::Playing)
                renderHUD(cmd);
            else
                renderStateOverlay(cmd);
        };
        m_renderGraph.addPass(std::move(tonemapPass));
    }
    m_renderGraph.execute(*cmd);

    m_device->submitFrame(cmd);
}

void Engine::renderStateOverlay(rhi::IRHICommandList* cmd) {
    const float w = static_cast<float>(m_windowWidth);
    const float h = static_cast<float>(m_windowHeight);

    m_ui.begin(m_windowWidth, m_windowHeight);

    // Dim the scene behind the overlay text.
    m_ui.drawQuad(0.f, 0.f, w, h, {0.f, 0.f, 0.f, 0.6f});

    // Settings sub-screen (task 44): when open it replaces the title/score/prompt
    // block with the slider/toggle panel, sharing this UI batch + flush.
    if (m_settingsOpen) {
        renderSettings(cmd);
        m_ui.flush(*m_device, cmd);
        return;
    }

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

    // Settings hint (task 44): a clickable "SETTINGS" line opening the panel.
    // The hit rect is recomputed identically in handleSettingsInput.
    {
        const char* hint = "SETTINGS  (S)";
        float scale      = 2.f;
        float tw         = m_ui.textWidth(hint, scale);
        m_ui.drawText((w - tw) * 0.5f, h * 0.70f + 8.f * 3.f + 16.f, hint, scale, {0.7f, 0.85f, 1.f, 1.f});
    }

    m_ui.flush(*m_device, cmd);
}

void Engine::dispatchParticleCompute(rhi::IRHICommandList* cmd) {
    m_computeAlphaCount    = 0;
    m_computeAdditiveCount = 0;
    if (!m_computeParticles)
        return;

    // Snapshot the live pool (alpha bucket first, then additive) and upload it to
    // the read-only state buffer. The compute sim then fills the instance buffer.
    m_particles.buildGpuParticles();
    const auto& state      = m_particles.gpuParticles();
    uint32_t total         = static_cast<uint32_t>(state.size());
    m_computeAlphaCount    = static_cast<uint32_t>(m_particles.gpuAlphaCount());
    m_computeAdditiveCount = total - m_computeAlphaCount;
    if (total == 0)
        return;

    m_device->uploadImmediate(m_particleStateBuffer, state.data(), total * sizeof(ParticleSystem::GpuParticle), 0);

    struct SimParams {
        float dt;
        float gravity;
        uint32_t count;
        float pad;
    } params{m_dt, 0.f, total, 0.f};

    // One thread per particle; 64 threads/group (matches numthreads in the shader).
    uint32_t groups = (total + 63u) / 64u;
    cmd->dispatchCompute(m_particleComputePipe, m_particleInstanceBuffer, m_particleStateBuffer, &params,
                         sizeof(params), groups, 1, 1);
}

// Look-sensitivity slider range (camera degrees-per-pixel). Volumes map 0..1.
namespace {
constexpr float kSensMin = 0.02f;
constexpr float kSensMax = 0.40f;
} // namespace

Engine::SettingsRect Engine::settingsTrackRect(int row) const {
    // Panel laid out centered; each row is a labeled track. Row indices:
    //   0 master, 1 sfx, 2 music, 3 ui, 4 sensitivity, 5 toggles/back row.
    const float w        = static_cast<float>(m_windowWidth);
    const float h        = static_cast<float>(m_windowHeight);
    const float trackW   = std::min(360.f, w * 0.5f);
    const float trackH   = 18.f;
    const float rowPitch = 46.f;
    const float startY   = h * 0.30f;
    const float trackX   = (w - trackW) * 0.5f + 90.f; // leave room for the label on the left
    SettingsRect r;
    r.x = trackX;
    r.y = startY + static_cast<float>(row) * rowPitch;
    r.w = trackW;
    r.h = trackH;
    return r;
}

void Engine::renderSettings(rhi::IRHICommandList* cmd) {
    (void)cmd; // drawing batches into the caller's m_ui; flush happens in caller.
    const float w = static_cast<float>(m_windowWidth);

    const char* title = "SETTINGS";
    {
        float scale = 5.f;
        float tw    = m_ui.textWidth(title, scale);
        m_ui.drawText((w - tw) * 0.5f, settingsTrackRect(0).y - 8.f * scale - 24.f, title, scale,
                      {0.9f, 0.95f, 1.f, 1.f});
    }

    struct SliderRow {
        const char* label;
        float value01; // normalized 0..1 for the fill
        int percent;   // displayed value
    };
    SliderRow rows[5] = {
        {"MASTER", m_settings.masterVolume, static_cast<int>(m_settings.masterVolume * 100.f + 0.5f)},
        {"SFX", m_settings.sfxVolume, static_cast<int>(m_settings.sfxVolume * 100.f + 0.5f)},
        {"MUSIC", m_settings.musicVolume, static_cast<int>(m_settings.musicVolume * 100.f + 0.5f)},
        {"UI", m_settings.uiVolume, static_cast<int>(m_settings.uiVolume * 100.f + 0.5f)},
        {"LOOK", std::clamp((m_settings.lookSensitivity - kSensMin) / (kSensMax - kSensMin), 0.f, 1.f),
         static_cast<int>(m_settings.lookSensitivity * 1000.f + 0.5f)},
    };

    for (int i = 0; i < 5; ++i) {
        SettingsRect t = settingsTrackRect(i);
        // Label to the left of the track.
        m_ui.drawText(t.x - 88.f, t.y + 2.f, rows[i].label, 2.f, {0.85f, 0.9f, 1.f, 1.f});
        // Track + fill.
        m_ui.drawQuad(t.x - 2.f, t.y - 2.f, t.w + 4.f, t.h + 4.f, {0.f, 0.f, 0.f, 0.6f});
        m_ui.drawQuad(t.x, t.y, t.w, t.h, {0.15f, 0.15f, 0.18f, 0.9f});
        float fillW = t.w * std::clamp(rows[i].value01, 0.f, 1.f);
        m_ui.drawQuad(t.x, t.y, fillW, t.h, {0.3f, 0.7f, 1.f, 0.95f});
        // Knob.
        m_ui.drawQuad(t.x + fillW - 3.f, t.y - 3.f, 6.f, t.h + 6.f, {1.f, 1.f, 1.f, 0.95f});
        // Value readout to the right.
        char vbuf[16];
        if (i == 4)
            std::snprintf(vbuf, sizeof(vbuf), "%d", rows[i].percent);
        else
            std::snprintf(vbuf, sizeof(vbuf), "%d%%", rows[i].percent);
        m_ui.drawText(t.x + t.w + 12.f, t.y + 2.f, vbuf, 2.f, {1.f, 0.95f, 0.7f, 1.f});
    }

    // Toggle row (row 5): vsync + fullscreen, each a clickable boxed label.
    {
        SettingsRect tr = settingsTrackRect(5);
        char vbuf[32];
        std::snprintf(vbuf, sizeof(vbuf), "VSYNC: %s", m_settings.vsync ? "ON" : "OFF");
        glm::vec4 vcol = m_settings.vsync ? glm::vec4{0.3f, 0.9f, 0.4f, 1.f} : glm::vec4{0.8f, 0.5f, 0.3f, 1.f};
        m_ui.drawText(tr.x - 88.f, tr.y + 2.f, vbuf, 2.f, vcol);

        char fbuf[40];
        std::snprintf(fbuf, sizeof(fbuf), "FULLSCREEN: %s", m_settings.fullscreen ? "ON" : "OFF");
        glm::vec4 fcol = m_settings.fullscreen ? glm::vec4{0.3f, 0.9f, 0.4f, 1.f} : glm::vec4{0.8f, 0.5f, 0.3f, 1.f};
        m_ui.drawText(tr.x + 140.f, tr.y + 2.f, fbuf, 2.f, fcol);
    }

    // Back hint.
    {
        const char* back = "BACK  (S or ESC)";
        float scale      = 2.f;
        SettingsRect tr  = settingsTrackRect(5);
        m_ui.drawText((w - m_ui.textWidth(back, scale)) * 0.5f, tr.y + 56.f, back, scale, {0.7f, 0.85f, 1.f, 1.f});
    }
}

bool Engine::handleSettingsInput(float mx, float my, bool pressed) {
    // Sliders 0..4. A press inside a track (or anywhere while a slider is being
    // dragged) sets that setting from the cursor's x within the track.
    for (int i = 0; i < 5; ++i) {
        SettingsRect t = settingsTrackRect(i);
        bool inRow = pressed && mx >= t.x - 6.f && mx <= t.x + t.w + 6.f && my >= t.y - 8.f && my <= t.y + t.h + 8.f;
        if (inRow)
            m_activeSlider = i;
        if (m_activeSlider == i) {
            float v01 = std::clamp((mx - t.x) / t.w, 0.f, 1.f);
            switch (i) {
            case 0:
                m_settings.masterVolume = v01;
                break;
            case 1:
                m_settings.sfxVolume = v01;
                break;
            case 2:
                m_settings.musicVolume = v01;
                break;
            case 3:
                m_settings.uiVolume = v01;
                break;
            case 4:
                m_settings.lookSensitivity = kSensMin + v01 * (kSensMax - kSensMin);
                break;
            default:
                break;
            }
            applyAndPersistSettings();
            return true;
        }
    }

    if (!pressed)
        return false;

    // Toggle row: vsync (left) / fullscreen (right) hit boxes around their text.
    SettingsRect tr = settingsTrackRect(5);
    if (my >= tr.y - 6.f && my <= tr.y + tr.h + 6.f) {
        if (mx >= tr.x - 88.f && mx < tr.x + 130.f) {
            m_settings.vsync = !m_settings.vsync;
            m_audio.playUI(kSfxUiClick);
            applyAndPersistSettings();
            return true;
        }
        if (mx >= tr.x + 132.f) {
            m_settings.fullscreen = !m_settings.fullscreen;
            m_audio.playUI(kSfxUiClick);
            applyAndPersistSettings();
            return true;
        }
    }
    return false;
}

void Engine::applyAndPersistSettings() {
    // Live-apply to the audio buses + camera, then persist. Window vsync /
    // fullscreen are recorded + saved but not applied at runtime (no swapchain
    // recreate path yet); they take effect on next launch.
    m_audio.setMasterVolume(m_settings.masterVolume);
    m_audio.setSfxVolume(m_settings.sfxVolume);
    m_audio.setMusicVolume(m_settings.musicVolume);
    m_audio.setUiVolume(m_settings.uiVolume);
    m_camera.lookSensitivity = m_settings.lookSensitivity;
    (void)saveSettings(paths::userDir(), m_settings);
}

void Engine::setRelativeMouse(bool relative) {
    if (relative == m_relativeMouse)
        return;
    m_relativeMouse = relative;
    SDL_SetWindowRelativeMouseMode(m_window, relative);
}

void Engine::renderParticles(rhi::IRHICommandList* cmd, const glm::mat4& viewProj) {
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

    uint64_t stride = sizeof(ParticleSystem::Instance);

    uint32_t alphaCount    = 0;
    uint32_t additiveCount = 0;

    if (m_computeParticles) {
        // Compute path: the instance buffer was already filled on-GPU by
        // dispatchParticleCompute() this frame; just draw the two sub-ranges.
        alphaCount    = m_computeAlphaCount;
        additiveCount = m_computeAdditiveCount;
    } else {
        // CPU path: build the two instance buckets and upload them packed
        // back-to-back into the shared instance buffer.
        m_particles.buildInstances();
        const auto& alpha    = m_particles.alphaInstances();
        const auto& additive = m_particles.additiveInstances();
        alphaCount           = static_cast<uint32_t>(alpha.size());
        additiveCount        = static_cast<uint32_t>(additive.size());

        if (alphaCount > 0)
            m_device->uploadImmediate(m_particleInstanceBuffer, alpha.data(), alphaCount * stride, 0);
        if (additiveCount > 0)
            m_device->uploadImmediate(m_particleInstanceBuffer, additive.data(), additiveCount * stride,
                                      alphaCount * stride);
    }

    if (alphaCount == 0 && additiveCount == 0)
        return;

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

    // --- Parry flash: brief cyan full-screen tint on a successful parry. ----
    if (m_parryFlash > 0.f) {
        float a = (m_parryFlash / 0.3f) * 0.35f;
        m_ui.drawQuad(0.f, 0.f, w, h, {0.3f, 0.8f, 1.f, a});
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

        // Hit marker overlay (task 29): a diagonal X over the crosshair while the
        // marker timer is active. Kills flash red, ordinary hits white.
        if (m_hitMarker.timer > 0.f) {
            glm::vec4 hm   = m_hitMarker.kill ? glm::vec4{1.f, 0.2f, 0.2f, 0.95f} : glm::vec4{1.f, 1.f, 1.f, 0.95f};
            const float ml = 8.f, mt = 2.f, off = 9.f;
            m_ui.drawQuad(cx - off - ml, cy - off - mt * 0.5f, ml, mt, hm);
            m_ui.drawQuad(cx + off, cy - off - mt * 0.5f, ml, mt, hm);
            m_ui.drawQuad(cx - off - ml, cy + off - mt * 0.5f, ml, mt, hm);
            m_ui.drawQuad(cx + off, cy + off - mt * 0.5f, ml, mt, hm);
        }
    }

    // --- Floating damage numbers (task 29): project each world hit to screen
    // and draw the amount, fading by remaining lifetime; kills use a hot color.
    {
        glm::mat4 viewProj =
            m_camera.projMatrix(static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight)) *
            m_camera.viewMatrix();
        for (const DamageEvent& ev : m_damageEvents) {
            ScreenProjection sp = worldToScreen(viewProj, ev.worldPos, m_windowWidth, m_windowHeight);
            if (!sp.visible)
                continue;
            float fade    = 1.f - (ev.lifetime > 0.f ? ev.age / ev.lifetime : 1.f);
            fade          = std::clamp(fade, 0.f, 1.f);
            glm::vec4 col = ev.killed ? glm::vec4{1.f, 0.85f, 0.2f, fade} : glm::vec4{1.f, 0.4f, 0.3f, fade};
            char dbuf[16];
            std::snprintf(dbuf, sizeof(dbuf), "%d", ev.amount);
            // Float the number up a little as it ages.
            m_ui.drawText(sp.screenPos.x, sp.screenPos.y - ev.age * 20.f, dbuf, 2.f, col);
        }
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

    // --- Weapon / ammo readout (bottom-right). Shows the upgrade-MODDED stats
    // (task 37) plus the alt-fire mode and active upgrade count.
    {
        WeaponComponent eff = effectiveWeapon();
        const char* name    = eff.type == WeaponType::Rocket   ? "ROCKET"
                              : eff.type == WeaponType::Plasma ? "PLASMA"
                                                               : "RIFLE";
        const char* altName = eff.type == WeaponType::Rocket   ? "CHARGE"
                              : eff.type == WeaponType::Plasma ? "OVERHEAT"
                                                               : "SPREAD";
        int upgradeCount =
            m_weaponIndex < m_weaponUpgrades.size() ? static_cast<int>(m_weaponUpgrades[m_weaponIndex].size()) : 0;
        char buf[128];
        if (eff.ammo < 0)
            std::snprintf(buf, sizeof(buf), "%zu:%s  DMG %d  RPS %.0f  +%d  ALT:%s", m_weaponIndex + 1, name,
                          eff.damage, eff.fireRate, upgradeCount, altName);
        else
            std::snprintf(buf, sizeof(buf), "%zu:%s  DMG %d  AMMO %d  +%d  ALT:%s", m_weaponIndex + 1, name, eff.damage,
                          eff.ammo, upgradeCount, altName);
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

    // --- Style meter (task 32): rank letter + fill bar, top-right under score.
    {
        const char* label = rankLabel(m_style.rank);
        float rscale      = 3.f;
        float rw          = m_ui.textWidth(label, rscale);
        float ry          = 24.f + 8.f * 2.f + 4.f + 8.f * 2.f + 8.f;
        // Higher ranks get a hotter color.
        float t        = std::clamp(m_style.points / m_styleConfig.maxPoints, 0.f, 1.f);
        glm::vec4 rcol = {1.f, 1.f - 0.6f * t, 0.2f + 0.2f * t, 1.f};
        m_ui.drawText(w - rw - 24.f, ry, label, rscale, rcol);

        // Fill bar beneath the rank letter showing progress within the meter.
        const float bw = 140.f, bh = 10.f;
        float bx = w - bw - 24.f;
        float by = ry + 8.f * rscale + 6.f;
        m_ui.drawQuad(bx - 2.f, by - 2.f, bw + 4.f, bh + 4.f, {0.f, 0.f, 0.f, 0.6f});
        m_ui.drawQuad(bx, by, bw, bh, {0.15f, 0.15f, 0.15f, 0.8f});
        m_ui.drawQuad(bx, by, bw * t, bh, rcol);
    }

    // --- Boss health bar (task 40): a wide top-center bar while a boss lives.
    {
        auto bossView = m_world.view<const BossComponent, const HealthComponent>();
        for (auto [e, boss, bossHealth] : bossView.each()) {
            float frac =
                boss.maxHealth > 0 ? static_cast<float>(bossHealth.current) / static_cast<float>(boss.maxHealth) : 0.f;
            frac = std::clamp(frac, 0.f, 1.f);

            const float bw = w * 0.6f, bh = 22.f;
            const float bx = (w - bw) * 0.5f, by = 56.f;
            m_ui.drawQuad(bx - 3.f, by - 3.f, bw + 6.f, bh + 6.f, {0.f, 0.f, 0.f, 0.7f}); // backing
            m_ui.drawQuad(bx, by, bw, bh, {0.1f, 0.1f, 0.1f, 0.85f});                     // track
            // Brighten the fill during the parryable vulnerable window.
            glm::vec4 fill =
                boss.vulnerableTimer > 0.f ? glm::vec4{1.f, 0.85f, 0.25f, 0.95f} : glm::vec4{0.85f, 0.1f, 0.15f, 0.95f};
            m_ui.drawQuad(bx, by, bw * frac, bh, fill);

            char bbuf[48];
            std::snprintf(bbuf, sizeof(bbuf), "BOSS  PHASE %d", boss.phase + 1);
            float scale = 2.f;
            float tw    = m_ui.textWidth(bbuf, scale);
            m_ui.drawText((w - tw) * 0.5f, by - 8.f * scale - 2.f, bbuf, scale, {1.f, 0.9f, 0.9f, 1.f});
            break; // one boss
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
