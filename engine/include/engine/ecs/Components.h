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
    rhi::RHIBuffer vertexBuffer{};
    rhi::RHIBuffer indexBuffer{};
    uint32_t indexCount      = 0;
    rhi::IndexType indexType = rhi::IndexType::Uint16;
};

struct MaterialComponent {
    rhi::RHITexture albedo{};
    rhi::RHISampler sampler{};
};

struct HealthComponent {
    int current = 100;
    int max     = 100;

    bool alive() const { return current > 0; }
};

struct EnemyComponent {
    enum class State : uint8_t { Idle, Chase, Attack };
    int health             = 100;
    State state            = State::Idle;
    float detectionRange   = 15.f;
    float attackRange      = 1.5f;
    float moveSpeed        = 3.f;
    uint32_t physicsBodyId = 0;
    float attackCooldown   = 0.f; // seconds until this enemy can attack again
    float attackInterval   = 1.f; // seconds between attacks
    int attackDamage       = 10;
};

struct SpawnPoint {
    glm::vec3 position{};
};

// A point light. position is world-space (mirrors a Transform when present,
// but kept here so lights without geometry still work). radius bounds the
// influence; intensity scales the inverse-square falloff.
struct LightComponent {
    glm::vec3 position{0.f};
    glm::vec3 color{1.f};
    float radius    = 8.f;
    float intensity = 1.f;
};

// How a weapon delivers damage. Hitscan resolves instantly via a ray cast;
// Rocket/Plasma spawn a moving ProjectileComponent entity (see ProjectileSystem).
enum class WeaponType : uint8_t { Hitscan, Rocket, Plasma };

struct WeaponComponent {
    int damage          = 25;
    float fireRate      = 5.f; // shots per second
    float cooldown      = 0.f;
    bool firedThisFrame = false;

    WeaponType type = WeaponType::Hitscan;
    // Projectile tuning (ignored for Hitscan). projectileSpeed in units/s,
    // projectileLifetime in seconds, splashRadius in units (0 = no splash).
    float projectileSpeed    = 30.f;
    float projectileLifetime = 5.f;
    float splashRadius       = 0.f;
    int ammo                 = -1;         // -1 = infinite
    glm::vec3 muzzleOffset{0.f, 0.f, 0.f}; // forward offset from the eye, scaled by look dir
};

// A flying projectile (rocket / plasma bolt). Integrated manually each frame by
// ProjectileSystem; no Jolt body. On hit it applies direct damage to the struck
// enemy plus radial splash with linear falloff, then is destroyed.
struct ProjectileComponent {
    glm::vec3 velocity{0.f};
    int damage           = 50;
    float lifetime       = 5.f; // remaining seconds before self-destruct
    float splashRadius   = 0.f; // 0 = direct-hit only, no splash
    uint32_t ownerBodyId = 0;   // physics body to ignore on the hit ray (the shooter)
};

// Apply damage to health, gated by an invulnerability (i-frame) timer.
// If iFrames > 0 the hit is ignored. Otherwise health is reduced and iFrames
// is set to iFrameDuration. Returns true if damage was actually applied.
// Reusable by movement (dash) and combat systems; kept pure for testing.
inline bool applyDamage(HealthComponent& health, float& iFrames, int amount, float iFrameDuration) {
    if (iFrames > 0.f || amount <= 0)
        return false;
    health.current -= amount;
    if (health.current < 0)
        health.current = 0;
    iFrames = iFrameDuration;
    return true;
}

} // namespace ds
