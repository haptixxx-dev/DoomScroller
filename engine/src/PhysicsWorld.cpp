#include "engine/PhysicsWorld.h"

#include <Jolt/Jolt.h>
// Jolt.h must precede all other Jolt headers (defines JPH_ASSERT etc.)

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <mimalloc.h>
#include <thread>

namespace ds {

// ---------------------------------------------------------------------------
// Object / broad-phase layer setup (minimal 2-layer config)
// ---------------------------------------------------------------------------

namespace Layers {
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING     = 1;
static constexpr JPH::uint NUM_LAYERS        = 2;
} // namespace Layers

namespace BPLayers {
static constexpr JPH::BroadPhaseLayer NON_MOVING{0};
static constexpr JPH::BroadPhaseLayer MOVING{1};
static constexpr JPH::uint NUM_LAYERS{2};
} // namespace BPLayers

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
    JPH::BroadPhaseLayer m_map[Layers::NUM_LAYERS]{};

  public:
    BPLayerInterface() {
        m_map[Layers::NON_MOVING] = BPLayers::NON_MOVING;
        m_map[Layers::MOVING]     = BPLayers::MOVING;
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return BPLayers::NUM_LAYERS; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return m_map[inLayer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer)) {
        case static_cast<JPH::BroadPhaseLayer::Type>(BPLayers::NON_MOVING):
            return "NON_MOVING";
        case static_cast<JPH::BroadPhaseLayer::Type>(BPLayers::MOVING):
            return "MOVING";
        default:
            return "INVALID";
        }
    }
#endif
};

class ObjVsBPFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer inObj, JPH::BroadPhaseLayer inBP) const override {
        switch (inObj) {
        case Layers::NON_MOVING:
            return inBP == BPLayers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            return false;
        }
    }
};

class ObjPairFilter final : public JPH::ObjectLayerPairFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        switch (a) {
        case Layers::NON_MOVING:
            return b == Layers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            return false;
        }
    }
};

// ---------------------------------------------------------------------------
// PhysicsWorld::Impl
// ---------------------------------------------------------------------------

struct PhysicsWorld::Impl {
    BPLayerInterface bpLayerInterface;
    ObjVsBPFilter objVsBPFilter;
    ObjPairFilter objPairFilter;
    JPH::TempAllocatorImpl tempAllocator{10 * 1024 * 1024};
    JPH::JobSystemThreadPool jobSystem;
    JPH::PhysicsSystem system;

    Impl()
        : jobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                    static_cast<int>(std::thread::hardware_concurrency()) - 1) {
        system.Init(1024, 0, 1024, 1024, bpLayerInterface, objVsBPFilter, objPairFilter);
    }
};

// ---------------------------------------------------------------------------
// PhysicsWorld
// ---------------------------------------------------------------------------

PhysicsWorld::PhysicsWorld()  = default;
PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::init() {
    // Route ALL Jolt allocations through mimalloc explicitly.
    // On macOS, posix_memalign uses a different malloc zone than free(), causing
    // "pointer being freed was not allocated" on Jolt thread pool shutdown.
    JPH::Allocate        = [](size_t n) -> void* { return mi_malloc(n); };
    JPH::Reallocate      = [](void* p, size_t, size_t n) -> void* { return mi_realloc(p, n); };
    JPH::Free            = [](void* p) { mi_free(p); };
    JPH::AlignedAllocate = [](size_t n, size_t align) -> void* { return mi_malloc_aligned(n, align); };
    JPH::AlignedFree     = [](void* p) { mi_free(p); };

    if (!JPH::Factory::sInstance)
        JPH::Factory::sInstance = new JPH::Factory();

    JPH::RegisterTypes();

    m_impl = std::make_unique<Impl>();
}

void PhysicsWorld::step(float dt, int collisionSteps) {
    m_impl->system.Update(dt, collisionSteps, &m_impl->tempAllocator, &m_impl->jobSystem);
}

uint32_t PhysicsWorld::addStaticBox(glm::vec3 center, glm::vec3 halfExtents) {
    JPH::BoxShapeSettings shapeSettings(JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
    shapeSettings.SetEmbedded();

    JPH::ShapeRefC shape = shapeSettings.Create().Get();

    JPH::BodyCreationSettings bcs(shape, JPH::RVec3(center.x, center.y, center.z), JPH::Quat::sIdentity(),
                                  JPH::EMotionType::Static, Layers::NON_MOVING);

    JPH::BodyInterface& bi = m_impl->system.GetBodyInterface();
    JPH::BodyID id         = bi.CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
    return id.GetIndexAndSequenceNumber();
}

uint32_t PhysicsWorld::addCapsule(float halfHeight, float radius, glm::vec3 position) {
    JPH::CapsuleShapeSettings shapeSettings(halfHeight, radius);
    shapeSettings.SetEmbedded();

    JPH::ShapeRefC shape = shapeSettings.Create().Get();

    JPH::BodyCreationSettings bcs(shape, JPH::RVec3(position.x, position.y, position.z), JPH::Quat::sIdentity(),
                                  JPH::EMotionType::Dynamic, Layers::MOVING);
    bcs.mLinearDamping  = 0.1f;
    bcs.mAngularDamping = 1.0f;
    bcs.mGravityFactor  = 1.0f;

    JPH::BodyInterface& bi = m_impl->system.GetBodyInterface();
    JPH::BodyID id         = bi.CreateAndAddBody(bcs, JPH::EActivation::Activate);
    return id.GetIndexAndSequenceNumber();
}

glm::vec3 PhysicsWorld::getPosition(uint32_t bodyId) const {
    JPH::BodyID jid = JPH::BodyID(bodyId);
    JPH::RVec3 pos  = m_impl->system.GetBodyInterface().GetPosition(jid);
    return {static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()), static_cast<float>(pos.GetZ())};
}

void PhysicsWorld::setPosition(uint32_t bodyId, glm::vec3 position) {
    m_impl->system.GetBodyInterface().SetPosition(JPH::BodyID(bodyId), JPH::RVec3(position.x, position.y, position.z),
                                                  JPH::EActivation::Activate);
}

glm::vec3 PhysicsWorld::getLinearVelocity(uint32_t bodyId) const {
    JPH::Vec3 v = m_impl->system.GetBodyInterface().GetLinearVelocity(JPH::BodyID(bodyId));
    return {v.GetX(), v.GetY(), v.GetZ()};
}

void PhysicsWorld::setLinearVelocity(uint32_t bodyId, glm::vec3 vel) {
    m_impl->system.GetBodyInterface().SetLinearVelocity(JPH::BodyID(bodyId), JPH::Vec3(vel.x, vel.y, vel.z));
}

void PhysicsWorld::addImpulse(uint32_t bodyId, glm::vec3 impulse) {
    m_impl->system.GetBodyInterface().AddImpulse(JPH::BodyID(bodyId), JPH::Vec3(impulse.x, impulse.y, impulse.z));
}

namespace {
struct ExcludeBodyFilter final : public JPH::BodyFilter {
    JPH::BodyID m_skip;
    explicit ExcludeBodyFilter(JPH::BodyID skip) : m_skip(skip) {}
    bool ShouldCollideLocked(const JPH::Body& b) const override { return b.GetID() != m_skip; }
};
} // anonymous namespace

bool PhysicsWorld::castRayDown(glm::vec3 origin, float dist, uint32_t excludeId) const {
    ExcludeBodyFilter filter{JPH::BodyID(excludeId)};

    JPH::RayCastResult result;
    JPH::RRayCast ray{JPH::RVec3(origin.x, origin.y, origin.z), JPH::Vec3(0.f, -dist, 0.f)};

    return m_impl->system.GetNarrowPhaseQuery().CastRay(ray, result, JPH::BroadPhaseLayerFilter{},
                                                        JPH::ObjectLayerFilter{}, filter);
}

uint32_t PhysicsWorld::castRay(glm::vec3 origin, glm::vec3 dir, float maxDist, uint32_t excludeId) const {
    ExcludeBodyFilter filter{JPH::BodyID(excludeId)};

    JPH::RayCastResult result;
    JPH::RRayCast ray{JPH::RVec3(origin.x, origin.y, origin.z),
                      JPH::Vec3(dir.x * maxDist, dir.y * maxDist, dir.z * maxDist)};

    bool hit = m_impl->system.GetNarrowPhaseQuery().CastRay(ray, result, JPH::BroadPhaseLayerFilter{},
                                                            JPH::ObjectLayerFilter{}, filter);

    if (!hit)
        return UINT32_MAX;
    return result.mBodyID.GetIndexAndSequenceNumber();
}

uint32_t PhysicsWorld::castRay(glm::vec3 origin, glm::vec3 dir, float maxDist, uint32_t excludeId,
                               glm::vec3& outHitPoint) const {
    ExcludeBodyFilter filter{JPH::BodyID(excludeId)};

    JPH::RayCastResult result;
    JPH::Vec3 disp(dir.x * maxDist, dir.y * maxDist, dir.z * maxDist);
    JPH::RRayCast ray{JPH::RVec3(origin.x, origin.y, origin.z), disp};

    bool hit = m_impl->system.GetNarrowPhaseQuery().CastRay(ray, result, JPH::BroadPhaseLayerFilter{},
                                                            JPH::ObjectLayerFilter{}, filter);

    if (!hit)
        return UINT32_MAX;

    JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
    outHitPoint      = {static_cast<float>(point.GetX()), static_cast<float>(point.GetY()),
                        static_cast<float>(point.GetZ())};
    return result.mBodyID.GetIndexAndSequenceNumber();
}

} // namespace ds
