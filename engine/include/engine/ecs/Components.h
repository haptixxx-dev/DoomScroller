#pragma once

#include "engine/rhi/RHITypes.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ds {

struct Transform {
    glm::vec3 position{0.f};
    glm::quat rotation{glm::identity<glm::quat>()};
    glm::vec3 scale{1.f};

    glm::mat4 modelMatrix() const {
        glm::mat4 t = glm::translate(glm::mat4(1.f), position);
        glm::mat4 r = glm::mat4_cast(rotation);
        glm::mat4 s = glm::scale(glm::mat4(1.f), scale);
        return t * r * s;
    }
};

struct MeshComponent {
    rhi::RHIBuffer  vertexBuffer{};
    rhi::RHIBuffer  indexBuffer{};
    uint32_t        indexCount = 0;
    rhi::IndexType  indexType  = rhi::IndexType::Uint16;
};

struct MaterialComponent {
    rhi::RHITexture albedo{};
    rhi::RHISampler sampler{};
};

struct EnemyComponent {
    enum class State : uint8_t { Idle, Chase, Attack };
    int      health         = 100;
    State    state          = State::Idle;
    float    detectionRange = 15.f;
    float    attackRange    = 1.5f;
    float    moveSpeed      = 3.f;
    uint32_t physicsBodyId  = 0;
};

struct SpawnPoint {
    glm::vec3 position{};
};

struct WeaponComponent {
    int   damage      = 25;
    float fireRate    = 5.f;  // shots per second
    float cooldown    = 0.f;
    bool  firedThisFrame = false;
};

} // namespace ds
