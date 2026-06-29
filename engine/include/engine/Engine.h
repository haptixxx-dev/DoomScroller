#pragma once

#include "engine/Camera.h"
#include "engine/PhysicsWorld.h"
#include "engine/PlayerController.h"
#include "engine/TextureManager.h"
#include "engine/Vertex.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/EnemySystem.h"
#include "engine/ecs/RenderSystem.h"

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
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

class Engine {
  public:
    explicit Engine(const EngineConfig& cfg);
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    void run();

    rhi::IRHIDevice& device() { return *m_device; }
    entt::registry&  world()  { return m_world; }

  private:
    void processEvents();
    void update();
    void render();
    void initScene();
    void buildArena(rhi::RHITexture albedo, rhi::RHISampler sampler);
    entt::entity spawnEnemy(glm::vec3 position, rhi::RHITexture albedo, rhi::RHISampler sampler);
    void fireWeapon();

    SDL_Window* m_window = nullptr;
    std::unique_ptr<rhi::IRHIDevice> m_device;
    bool m_running = false;

    rhi::RHIShader   m_meshVS       = {};
    rhi::RHIShader   m_meshFS       = {};
    rhi::RHIPipeline m_meshPipeline = {};
    rhi::RHITexture  m_depthTexture = {};
    rhi::RHISampler  m_linearSampler = {};

    std::unique_ptr<TextureManager> m_textures;
    PhysicsWorld m_physics;
    uint32_t m_playerBodyId = 0;
    std::unique_ptr<PlayerController> m_player;
    WeaponComponent m_weapon;
    bool m_firePressed = false;
    entt::registry m_world;

    Camera m_camera;
    int m_windowWidth  = 1280;
    int m_windowHeight = 720;

    uint64_t m_lastTick = 0;
    float m_dt          = 0.f;
};

} // namespace ds
