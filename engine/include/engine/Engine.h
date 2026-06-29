#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string_view>

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

  private:
    void processEvents();
    void update();
    void render();
    void initTriangle();

    SDL_Window* m_window = nullptr;
    std::unique_ptr<rhi::IRHIDevice> m_device;
    bool m_running = false;

    rhi::RHIShader m_triangleVS         = {};
    rhi::RHIShader m_triangleFS         = {};
    rhi::RHIPipeline m_trianglePipeline = {};
};

} // namespace ds
