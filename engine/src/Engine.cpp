#include "engine/Engine.h"

#include "engine/Paths.h"
#include "engine/Profiler.h"
#include "engine/ShaderLoader.h"

#include <SDL3/SDL_gpu.h>

#include <stdexcept>

#include "rhi/sdl3/SDL3Device.h"

namespace ds {

Engine::Engine(const EngineConfig& cfg) {
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());

    m_window = SDL_CreateWindow(cfg.title.data(), cfg.width, cfg.height, 0);
    if (!m_window)
        throw std::runtime_error(SDL_GetError());

    m_device  = rhi::createDevice(m_window);
    m_running = true;

    initTriangle();
}

Engine::~Engine() {
    if (m_trianglePipeline.valid())
        m_device->destroyPipeline(m_trianglePipeline);
    if (m_triangleVS.valid())
        m_device->destroyShader(m_triangleVS);
    if (m_triangleFS.valid())
        m_device->destroyShader(m_triangleFS);
    m_device.reset();
    if (m_window)
        SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void Engine::initTriangle() {
    ShaderLoader loader(static_cast<SDL_GPUDevice*>(m_device->nativeDevice()), paths::shaders());
    m_triangleVS = loader.load(*m_device, "triangle", rhi::ShaderStage::Vertex);
    m_triangleFS = loader.load(*m_device, "triangle", rhi::ShaderStage::Fragment);

    rhi::ColorTargetDesc colorTarget{};
    colorTarget.format = m_device->swapchainFormat();

    rhi::PipelineDesc pipeDesc{};
    pipeDesc.vertexShader   = m_triangleVS;
    pipeDesc.fragmentShader = m_triangleFS;
    pipeDesc.colorTargets   = {&colorTarget, 1};
    pipeDesc.hasDepth       = false;
    pipeDesc.depthTest      = false;
    pipeDesc.depthWrite     = false;
    pipeDesc.cullMode       = rhi::CullMode::None;

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
        if (e.type == SDL_EVENT_QUIT)
            m_running = false;
    }
}

void Engine::update() {
    DS_ZONE();
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

    rhi::RenderPassDesc pass{};
    pass.colorAttachments = {&color, 1};

    cmd->beginRenderPass(pass);
    cmd->setPipeline(m_trianglePipeline);
    cmd->draw(3, 1, 0, 0);
    cmd->endRenderPass();

    m_device->submitFrame(cmd);
}

} // namespace ds
