#include "engine/Engine.h"

#include "engine/Paths.h"
#include "engine/Profiler.h"
#include "engine/ShaderLoader.h"

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

#include "rhi/sdl3/SDL3Device.h"

namespace ds {

// Colored cube: 8 unique corners, 36 indices (12 triangles, 6 faces)
static const Vertex kCubeVerts[] = {
    {{-0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}},  // 0 red
    {{0.5f, -0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},   // 1 green
    {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},    // 2 blue
    {{-0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}},   // 3 yellow
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}}, // 4 magenta
    {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}},  // 5 cyan
    {{0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}},   // 6 white
    {{-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.0f}},  // 7 olive
};

static const uint16_t kCubeIdx[] = {
    0, 1, 2, 0, 2, 3, // front  (+Z)
    5, 4, 7, 5, 7, 6, // back   (-Z)
    4, 0, 3, 4, 3, 7, // left   (-X)
    1, 5, 6, 1, 6, 2, // right  (+X)
    3, 2, 6, 3, 6, 7, // top    (+Y)
    4, 5, 1, 4, 1, 0, // bottom (-Y)
};

Engine::Engine(const EngineConfig& cfg) : m_windowWidth(cfg.width), m_windowHeight(cfg.height) {
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());

    m_window = SDL_CreateWindow(cfg.title.data(), cfg.width, cfg.height, 0);
    if (!m_window)
        throw std::runtime_error(SDL_GetError());

    SDL_SetWindowRelativeMouseMode(m_window, true);

    m_device  = rhi::createDevice(m_window);
    m_running = true;

    initScene();

    m_lastTick = SDL_GetPerformanceCounter();
}

Engine::~Engine() {
    if (m_trianglePipeline.valid())
        m_device->destroyPipeline(m_trianglePipeline);
    if (m_triangleVS.valid())
        m_device->destroyShader(m_triangleVS);
    if (m_triangleFS.valid())
        m_device->destroyShader(m_triangleFS);
    if (m_vertexBuffer.valid())
        m_device->destroyBuffer(m_vertexBuffer);
    if (m_indexBuffer.valid())
        m_device->destroyBuffer(m_indexBuffer);
    if (m_depthTexture.valid())
        m_device->destroyTexture(m_depthTexture);
    m_device.reset();
    if (m_window)
        SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void Engine::initScene() {
    ShaderLoader loader(static_cast<SDL_GPUDevice*>(m_device->nativeDevice()), paths::shaders());
    m_triangleVS = loader.load(*m_device, "triangle", rhi::ShaderStage::Vertex, 0, 1);
    m_triangleFS = loader.load(*m_device, "triangle", rhi::ShaderStage::Fragment);

    // Vertex buffer
    rhi::BufferDesc vbDesc{};
    vbDesc.size      = sizeof(kCubeVerts);
    vbDesc.usage     = rhi::BufferUsage::Vertex;
    vbDesc.debugName = "cube_verts";
    m_vertexBuffer   = m_device->createBuffer(vbDesc);
    m_device->uploadImmediate(m_vertexBuffer, kCubeVerts, sizeof(kCubeVerts));

    // Index buffer
    m_indexCount = static_cast<uint32_t>(sizeof(kCubeIdx) / sizeof(kCubeIdx[0]));
    rhi::BufferDesc ibDesc{};
    ibDesc.size      = sizeof(kCubeIdx);
    ibDesc.usage     = rhi::BufferUsage::Index;
    ibDesc.debugName = "cube_idx";
    m_indexBuffer    = m_device->createBuffer(ibDesc);
    m_device->uploadImmediate(m_indexBuffer, kCubeIdx, sizeof(kCubeIdx));

    // Depth texture
    rhi::TextureDesc depthDesc{};
    depthDesc.width          = static_cast<uint32_t>(m_windowWidth);
    depthDesc.height         = static_cast<uint32_t>(m_windowHeight);
    depthDesc.format         = rhi::TextureFormat::D32Float;
    depthDesc.isDepthStencil = true;
    depthDesc.debugName      = "depth";
    m_depthTexture           = m_device->createTexture(depthDesc);

    // Vertex layout: interleaved pos + color from binding 0
    rhi::VertexAttribute attrs[2];
    attrs[0].location     = 0;
    attrs[0].binding      = 0;
    attrs[0].offset       = 0;
    attrs[0].elementCount = 3;
    attrs[0].isFloat      = true;
    attrs[1].location     = 1;
    attrs[1].binding      = 0;
    attrs[1].offset       = sizeof(glm::vec3);
    attrs[1].elementCount = 3;
    attrs[1].isFloat      = true;

    rhi::VertexBinding vbinding{};
    vbinding.binding   = 0;
    vbinding.stride    = static_cast<uint32_t>(sizeof(Vertex));
    vbinding.instanced = false;

    rhi::ColorTargetDesc colorTarget{};
    colorTarget.format = m_device->swapchainFormat();

    rhi::PipelineDesc pipeDesc{};
    pipeDesc.vertexShader     = m_triangleVS;
    pipeDesc.fragmentShader   = m_triangleFS;
    pipeDesc.vertexAttributes = {attrs, 2};
    pipeDesc.vertexBindings   = {&vbinding, 1};
    pipeDesc.colorTargets     = {&colorTarget, 1};
    pipeDesc.hasDepth         = true;
    pipeDesc.depthTest        = true;
    pipeDesc.depthWrite       = true;
    pipeDesc.depthFormat      = rhi::TextureFormat::D32Float;
    pipeDesc.depthCompare     = rhi::CompareOp::Less;
    pipeDesc.cullMode         = rhi::CullMode::Back;

    m_trianglePipeline = m_device->createPipeline(pipeDesc);
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
        default:
            break;
        }
    }
}

void Engine::update() {
    DS_ZONE();

    uint64_t now = SDL_GetPerformanceCounter();
    m_dt         = static_cast<float>(now - m_lastTick) / static_cast<float>(SDL_GetPerformanceFrequency());
    m_dt         = std::min(m_dt, 0.05f); // cap at 50ms to prevent spiral-of-death
    m_lastTick   = now;

    const bool* keys = SDL_GetKeyboardState(nullptr);
    glm::vec3 dir{0.f};
    if (keys[SDL_SCANCODE_W])
        dir.z += 1.f;
    if (keys[SDL_SCANCODE_S])
        dir.z -= 1.f;
    if (keys[SDL_SCANCODE_A])
        dir.x -= 1.f;
    if (keys[SDL_SCANCODE_D])
        dir.x += 1.f;
    if (keys[SDL_SCANCODE_SPACE])
        dir.y += 1.f;
    if (keys[SDL_SCANCODE_LCTRL])
        dir.y -= 1.f;
    if (glm::length(dir) > 0.f)
        m_camera.moveLocal(glm::normalize(dir), m_dt);
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
    cmd->setPipeline(m_trianglePipeline);

    float aspect  = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
    glm::mat4 mvp = m_camera.projMatrix(aspect) * m_camera.viewMatrix();
    cmd->pushVertexConstants(&mvp, sizeof(mvp));

    cmd->setVertexBuffer(0, m_vertexBuffer);
    cmd->setIndexBuffer(m_indexBuffer, rhi::IndexType::Uint16);
    cmd->drawIndexed(m_indexCount);

    cmd->endRenderPass();

    m_device->submitFrame(cmd);
}

} // namespace ds
