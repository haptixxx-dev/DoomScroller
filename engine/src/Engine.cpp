#include "engine/Engine.h"

#include "engine/Paths.h"
#include "engine/Profiler.h"
#include "engine/PlayerController.h"
#include "engine/ShaderLoader.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/EnemySystem.h"

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

#include "rhi/sdl3/SDL3Device.h"

namespace ds {

static MeshComponent makeBoxMesh(rhi::IRHIDevice& device,
                                  float hw, float hh, float hd,
                                  glm::vec3 color) {
    struct BoxVert { glm::vec3 pos; glm::vec3 col; glm::vec2 uv; glm::vec3 normal; };

    const BoxVert verts[24] = {
        // +Y top
        {{-hw,hh,-hd},color,{0,0},{0,1,0}}, {{ hw,hh,-hd},color,{1,0},{0,1,0}},
        {{ hw,hh, hd},color,{1,1},{0,1,0}}, {{-hw,hh, hd},color,{0,1},{0,1,0}},
        // -Y bottom
        {{-hw,-hh, hd},color,{0,0},{0,-1,0}}, {{ hw,-hh, hd},color,{1,0},{0,-1,0}},
        {{ hw,-hh,-hd},color,{1,1},{0,-1,0}}, {{-hw,-hh,-hd},color,{0,1},{0,-1,0}},
        // +Z front
        {{-hw,-hh,hd},color,{0,0},{0,0,1}}, {{ hw,-hh,hd},color,{1,0},{0,0,1}},
        {{ hw, hh,hd},color,{1,1},{0,0,1}}, {{-hw, hh,hd},color,{0,1},{0,0,1}},
        // -Z back
        {{ hw,-hh,-hd},color,{0,0},{0,0,-1}}, {{-hw,-hh,-hd},color,{1,0},{0,0,-1}},
        {{-hw, hh,-hd},color,{1,1},{0,0,-1}}, {{ hw, hh,-hd},color,{0,1},{0,0,-1}},
        // +X right
        {{ hw,-hh, hd},color,{0,0},{1,0,0}}, {{ hw,-hh,-hd},color,{1,0},{1,0,0}},
        {{ hw, hh,-hd},color,{1,1},{1,0,0}}, {{ hw, hh, hd},color,{0,1},{1,0,0}},
        // -X left
        {{-hw,-hh,-hd},color,{0,0},{-1,0,0}}, {{-hw,-hh, hd},color,{1,0},{-1,0,0}},
        {{-hw, hh, hd},color,{1,1},{-1,0,0}}, {{-hw, hh,-hd},color,{0,1},{-1,0,0}},
    };
    const uint16_t idx[36] = {
        0,1,2,0,2,3,  4,5,6,4,6,7,  8,9,10,8,10,11,
        12,13,14,12,14,15, 16,17,18,16,18,19, 20,21,22,20,22,23
    };

    rhi::BufferDesc vbd{};
    vbd.size  = sizeof(verts);
    vbd.usage = rhi::BufferUsage::Vertex;
    auto vb = device.createBuffer(vbd);
    device.uploadImmediate(vb, verts, vbd.size);

    rhi::BufferDesc ibd{};
    ibd.size  = sizeof(idx);
    ibd.usage = rhi::BufferUsage::Index;
    auto ib = device.createBuffer(ibd);
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
            p[3]                = 255u;
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

    m_device  = rhi::createDevice(m_window);
    m_textures = std::make_unique<TextureManager>(*m_device);
    m_running = true;

    m_physics.init();
    initScene();

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
    m_meshFS = loader.load(*m_device, "mesh", rhi::ShaderStage::Fragment, 1, 0);

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
    pipeDesc.depthCompare = rhi::CompareOp::Less;
    pipeDesc.cullMode     = rhi::CullMode::None;
    m_meshPipeline = m_device->createPipeline(pipeDesc);

    // Checkerboard placeholder texture
    constexpr uint32_t kTexW = 8, kTexH = 8;
    uint8_t pixels[kTexW * kTexH * 4];
    makeCheckerboard(pixels, kTexW, kTexH);
    rhi::RHITexture albedo = m_textures->createFromMemory(pixels, kTexW, kTexH, "checkerboard");

    // Camera starts inside the arena at eye height
    m_camera.position = {0.f, 1.7f, 0.f};
    m_camera.yaw      = -90.f;

    buildArena(albedo, m_linearSampler);

    // Player capsule: radius 0.4, halfHeight 0.5 (total height ~1.8 units)
    m_playerBodyId = m_physics.addCapsule(0.5f, 0.4f, {0.f, 1.7f, 0.f});
    m_player = std::make_unique<PlayerController>(m_physics, m_playerBodyId);

    // Spawn enemies at corners of the arena
    spawnEnemy({-7.f, 1.5f, -7.f}, albedo, m_linearSampler);
    spawnEnemy({ 7.f, 1.5f, -7.f}, albedo, m_linearSampler);
    spawnEnemy({ 0.f, 1.5f,  7.f}, albedo, m_linearSampler);
}

void Engine::fireWeapon() {
    glm::vec3 origin = m_camera.position;
    glm::vec3 dir    = glm::normalize(m_camera.front());
    uint32_t hitId   = m_physics.castRay(origin, dir, 100.f, m_playerBodyId);
    if (hitId == UINT32_MAX)
        return;

    auto view = m_world.view<EnemyComponent>();
    for (auto [entity, enemy] : view.each()) {
        if (enemy.physicsBodyId == hitId) {
            enemy.health -= m_weapon.damage;
            break;
        }
    }
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
    m_world.emplace<EnemyComponent>(e, EnemyComponent{100, EnemyComponent::State::Idle,
                                                       15.f, 1.5f, 3.f, bodyId});
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
        { {{ {-W,0,-D},{1,1,1},{0,D}, {0,1,0} }, { {W,0,-D},{1,1,1},{W*2,D},{0,1,0} },
           { {W,0, D},{1,1,1},{W*2,0},{0,1,0} }, { {-W,0, D},{1,1,1},{0,0}, {0,1,0} }}, "floor" },
        // Ceiling (y=H), normal -Y
        { {{ {-W,H, D},{1,1,1},{0,D}, {0,-1,0} }, { {W,H, D},{1,1,1},{W*2,D},{0,-1,0} },
           { {W,H,-D},{1,1,1},{W*2,0},{0,-1,0} }, { {-W,H,-D},{1,1,1},{0,0}, {0,-1,0} }}, "ceil" },
        // North wall (z=-D), normal +Z (inward)
        { {{ {-W,H,-D},{1,1,1},{0,H}, {0,0,1} }, { {W,H,-D},{1,1,1},{W*2,H},{0,0,1} },
           { {W,0,-D},{1,1,1},{W*2,0},{0,0,1} }, { {-W,0,-D},{1,1,1},{0,0}, {0,0,1} }}, "wall_n" },
        // South wall (z=+D), normal -Z (inward)
        { {{ {W,H, D},{1,1,1},{0,H}, {0,0,-1} }, { {-W,H, D},{1,1,1},{W*2,H},{0,0,-1} },
           { {-W,0, D},{1,1,1},{W*2,0},{0,0,-1} }, { {W,0, D},{1,1,1},{0,0}, {0,0,-1} }}, "wall_s" },
        // West wall (x=-W), normal +X (inward)
        { {{ {-W,H,-D},{1,1,1},{0,H}, {1,0,0} }, { {-W,H, D},{1,1,1},{D*2,H},{1,0,0} },
           { {-W,0, D},{1,1,1},{D*2,0},{1,0,0} }, { {-W,0,-D},{1,1,1},{0,0}, {1,0,0} }}, "wall_w" },
        // East wall (x=+W), normal -X (inward)
        { {{ {W,H, D},{1,1,1},{0,H}, {-1,0,0} }, { {W,H,-D},{1,1,1},{D*2,H},{-1,0,0} },
           { {W,0,-D},{1,1,1},{D*2,0},{-1,0,0} }, { {W,0, D},{1,1,1},{0,0}, {-1,0,0} }}, "wall_e" },
    };

    static const uint16_t kQuadIdx[] = {0, 1, 2, 0, 2, 3};

    for (const auto& face : faces) {
        rhi::BufferDesc vbd{};
        vbd.size      = sizeof(face.verts);
        vbd.usage     = rhi::BufferUsage::Vertex;
        vbd.debugName = face.name;
        auto vb = m_device->createBuffer(vbd);
        m_device->uploadImmediate(vb, face.verts, vbd.size);

        rhi::BufferDesc ibd{};
        ibd.size      = sizeof(kQuadIdx);
        ibd.usage     = rhi::BufferUsage::Index;
        auto ib = m_device->createBuffer(ibd);
        m_device->uploadImmediate(ib, kQuadIdx, ibd.size);

        auto e = m_world.create();
        m_world.emplace<Transform>(e);
        m_world.emplace<MeshComponent>(e, MeshComponent{vb, ib, 6u, rhi::IndexType::Uint16});
        m_world.emplace<MaterialComponent>(e, MaterialComponent{albedo, sampler});
    }

    m_physics.addStaticBox({0.f, -T,  0.f}, {W, T, D});          // floor
    m_physics.addStaticBox({0.f, H+T, 0.f}, {W, T, D});          // ceiling
    m_physics.addStaticBox({0.f,  H/2, -D-T}, {W, H/2, T});      // north wall
    m_physics.addStaticBox({0.f,  H/2,  D+T}, {W, H/2, T});      // south wall
    m_physics.addStaticBox({-W-T, H/2, 0.f},  {T, H/2, D});      // west wall
    m_physics.addStaticBox({ W+T, H/2, 0.f},  {T, H/2, D});      // east wall
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
            if (e.key.key == SDLK_ESCAPE)
                m_running = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            m_camera.rotate(e.motion.xrel, e.motion.yrel);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT)
                m_firePressed = true;
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

    const bool* keys = SDL_GetKeyboardState(nullptr);

    // Project camera front/right onto XZ plane for ground-relative movement
    glm::vec3 camFront = m_camera.front();
    glm::vec3 fwd = glm::normalize(glm::vec3{camFront.x, 0.f, camFront.z});
    glm::vec3 rgt = m_camera.right();

    glm::vec3 moveDir{0.f};
    if (keys[SDL_SCANCODE_W]) moveDir += fwd;
    if (keys[SDL_SCANCODE_S]) moveDir -= fwd;
    if (keys[SDL_SCANCODE_D]) moveDir += rgt;
    if (keys[SDL_SCANCODE_A]) moveDir -= rgt;
    if (glm::length(moveDir) > 0.f)
        moveDir = glm::normalize(moveDir);

    bool jump = keys[SDL_SCANCODE_SPACE];

    m_physics.step(m_dt);
    m_player->update(m_camera, moveDir, jump, m_dt);
    enemySystem(m_world, m_physics, m_camera.position, m_dt);

    m_weapon.cooldown -= m_dt;
    m_weapon.firedThisFrame = false;
    if (m_firePressed && m_weapon.cooldown <= 0.f) {
        fireWeapon();
        m_weapon.cooldown    = 1.f / m_weapon.fireRate;
        m_weapon.firedThisFrame = true;
    }
    m_firePressed = false;
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

    RenderContext ctx{cmd, m_meshPipeline, viewProj};
    renderSystem(m_world, ctx);

    cmd->endRenderPass();

    m_device->submitFrame(cmd);
}

} // namespace ds
