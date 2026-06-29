#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>

namespace ds {

// Thin wrapper around Jolt PhysicsSystem.
// Jolt headers are kept out of this header (PIMPL).
class PhysicsWorld {
  public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&)            = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void init();
    void step(float dt, int collisionSteps = 1);

    // Add a static (non-moving) box body. Returns opaque body ID.
    uint32_t addStaticBox(glm::vec3 center, glm::vec3 halfExtents);

    // Add a dynamic capsule body. Returns opaque body ID.
    uint32_t addCapsule(float halfHeight, float radius, glm::vec3 position);

    glm::vec3 getPosition(uint32_t bodyId) const;
    glm::vec3 getLinearVelocity(uint32_t bodyId) const;
    void setLinearVelocity(uint32_t bodyId, glm::vec3 vel);
    void addImpulse(uint32_t bodyId, glm::vec3 impulse);

    // Returns true if a ray cast from origin downward hits something within dist.
    // Excludes the body identified by excludeId.
    bool castRayDown(glm::vec3 origin, float dist, uint32_t excludeId) const;
    // Returns hit body ID, or UINT32_MAX if no hit within maxDist. Excludes excludeId.
    uint32_t castRay(glm::vec3 origin, glm::vec3 dir, float maxDist, uint32_t excludeId) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ds
