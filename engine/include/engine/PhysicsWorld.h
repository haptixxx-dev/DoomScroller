#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

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

    // Add a dynamic (moving) box body with an initial linear velocity. Returns opaque body ID.
    uint32_t addDynamicBox(glm::vec3 center, glm::vec3 halfExtents, glm::vec3 initialVelocity = {});

    // Add a static triangle-mesh body (baked level geometry, e.g. a glTF-
    // converted or Lua-placed level piece). `vertices`/`indices` are in local
    // space; `position`/`rotation` place the body in world space. Jolt mesh
    // shapes are static-only, which matches level geometry's needs exactly.
    // Returns opaque body ID.
    uint32_t addStaticMesh(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices,
                           glm::vec3 position, glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f));

    // Remove and destroy a body from the physics system.
    void removeBody(uint32_t bodyId);

    glm::vec3 getPosition(uint32_t bodyId) const;
    void setPosition(uint32_t bodyId, glm::vec3 position);
    glm::vec3 getLinearVelocity(uint32_t bodyId) const;
    void setLinearVelocity(uint32_t bodyId, glm::vec3 vel);
    void addImpulse(uint32_t bodyId, glm::vec3 impulse);

    // Returns true if a ray cast from origin downward hits something within dist.
    // Excludes the body identified by excludeId.
    bool castRayDown(glm::vec3 origin, float dist, uint32_t excludeId) const;
    // Returns hit body ID, or UINT32_MAX if no hit within maxDist. Excludes excludeId.
    uint32_t castRay(glm::vec3 origin, glm::vec3 dir, float maxDist, uint32_t excludeId) const;

    // Like castRay, but also writes the world-space hit point into outHitPoint
    // when something is hit. outHitPoint is left untouched on a miss.
    uint32_t castRay(glm::vec3 origin, glm::vec3 dir, float maxDist, uint32_t excludeId, glm::vec3& outHitPoint) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ds
