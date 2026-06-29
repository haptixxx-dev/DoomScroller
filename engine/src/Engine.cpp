#include "engine/Engine.h"
#include <stdexcept>

namespace ds {

Engine::Engine(const EngineConfig& cfg) {
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());

    m_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV  |
                                   SDL_GPU_SHADERFORMAT_MSL    |
                                   SDL_GPU_SHADERFORMAT_DXIL,
                                   false, nullptr);
    if (!m_device)
        throw std::runtime_error(SDL_GetError());

    m_window = SDL_CreateWindow(cfg.title.data(), cfg.width, cfg.height, 0);
    if (!m_window)
        throw std::runtime_error(SDL_GetError());

    if (!SDL_ClaimWindowForGPUDevice(m_device, m_window))
        throw std::runtime_error(SDL_GetError());

    m_running = true;
}

Engine::~Engine() {
    if (m_device && m_window)
        SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
    if (m_window)
        SDL_DestroyWindow(m_window);
    if (m_device)
        SDL_DestroyGPUDevice(m_device);
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

void Engine::update() {
    // game logic goes here
}

void Engine::render() {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (!cmd) return;

    SDL_GPUTexture* swapchain = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, m_window, &swapchain, nullptr, nullptr)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    if (swapchain) {
        SDL_GPUColorTargetInfo color{};
        color.texture     = swapchain;
        color.load_op     = SDL_GPU_LOADOP_CLEAR;
        color.store_op    = SDL_GPU_STOREOP_STORE;
        color.clear_color = {0.05f, 0.05f, 0.08f, 1.0f};

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
        SDL_EndGPURenderPass(pass);
    }

    SDL_SubmitGPUCommandBuffer(cmd);
}

} // namespace ds
