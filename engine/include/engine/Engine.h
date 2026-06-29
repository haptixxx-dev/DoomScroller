#pragma once

#include "engine/Camera.h"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <memory>
#include <string_view>

#include "rhi/IRHIDevice.h"

namespace ds {

struct EngineConfig {
    std::string_view title = "DoomScroller";
    int width              = 1280;
    int height             = 720;
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
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
    void initScene();

    SDL_Window* m_window = nullptr;
    std::unique_ptr<rhi::IRHIDevice> m_device;
    bool m_running = false;

    rhi::RHIShader m_triangleVS         = {};
    rhi::RHIShader m_triangleFS         = {};
    rhi::RHIPipeline m_trianglePipeline = {};
    rhi::RHIBuffer m_vertexBuffer       = {};
    rhi::RHIBuffer m_indexBuffer        = {};
    rhi::RHITexture m_depthTexture      = {};
    uint32_t m_indexCount               = 0;

    Camera m_camera;
    int m_windowWidth  = 1280;
    int m_windowHeight = 720;

    uint64_t m_lastTick = 0;
    float m_dt          = 0.f;
};

} // namespace ds
