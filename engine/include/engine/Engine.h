#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <string_view>

namespace ds {

struct EngineConfig {
    std::string_view title  = "DoomScroller";
    int              width  = 1280;
    int              height = 720;
};

class Engine {
public:
    explicit Engine(const EngineConfig& cfg);
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    void run();

private:
    void processEvents();
    void update();
    void render();

    SDL_Window*    m_window  = nullptr;
    SDL_GPUDevice* m_device  = nullptr;
    bool           m_running = false;
};

} // namespace ds
