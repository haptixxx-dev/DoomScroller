#include "engine/Engine.h"
#include "rhi/sdl3/SDL3Device.h"
#include <stdexcept>

namespace ds {

Engine::Engine(const EngineConfig& cfg) {
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());

    m_window = SDL_CreateWindow(cfg.title.data(), cfg.width, cfg.height, 0);
    if (!m_window)
        throw std::runtime_error(SDL_GetError());

    m_device  = rhi::createDevice(m_window);
    m_running = true;
}

Engine::~Engine() {
    m_device.reset();
    if (m_window) SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void Engine::run() {
    while (m_running) {
        processEvents();
        update();
        render();
    }
}

void Engine::processEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT)
            m_running = false;
    }
}

void Engine::update() {}

void Engine::render() {
    rhi::IRHICommandList* cmd = m_device->beginFrame();
    if (!cmd) return;

    rhi::ColorAttachment color{};
    color.loadOp       = rhi::LoadOp::Clear;
    color.storeOp      = rhi::StoreOp::Store;
    color.clearColor[0] = 0.05f;
    color.clearColor[1] = 0.05f;
    color.clearColor[2] = 0.08f;
    color.clearColor[3] = 1.0f;

    rhi::RenderPassDesc pass{};
    pass.colorAttachments = { &color, 1 };

    cmd->beginRenderPass(pass);
    cmd->endRenderPass();

    m_device->submitFrame(cmd);
}

} // namespace ds
