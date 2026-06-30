#include "engine/ParticleSystem.h"

#include <glm/gtc/constants.hpp>

namespace ds {

ParticleSystem::ParticleSystem() {
    m_freeList.reserve(kMaxParticles);
    // Free list is a stack; push in reverse so index 0 is handed out first.
    for (std::size_t i = kMaxParticles; i-- > 0;)
        m_freeList.push_back(i);
}

float ParticleSystem::randUnit() {
    // xorshift32 — deterministic, fast, good enough for VFX jitter.
    m_rngState ^= m_rngState << 13;
    m_rngState ^= m_rngState >> 17;
    m_rngState ^= m_rngState << 5;
    return static_cast<float>(m_rngState & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

glm::vec3 ParticleSystem::randInSphere() {
    glm::vec3 v{randUnit() * 2.f - 1.f, randUnit() * 2.f - 1.f, randUnit() * 2.f - 1.f};
    float len = glm::length(v);
    if (len < 1e-4f)
        return {0.f, 1.f, 0.f};
    return v / len;
}

std::size_t ParticleSystem::allocate() {
    if (m_freeList.empty())
        return kInvalid;
    std::size_t idx = m_freeList.back();
    m_freeList.pop_back();
    return idx;
}

void ParticleSystem::update(float dt) {
    for (std::size_t i = 0; i < kMaxParticles; ++i) {
        Particle& p = m_particles[i];
        if (!p.alive)
            continue;

        p.life -= dt;
        if (p.life <= 0.f) {
            p.alive = false;
            m_freeList.push_back(i);
            --m_aliveCount;
            continue;
        }

        p.velocity.y -= p.gravity * dt;
        p.position += p.velocity * dt;

        // Fade alpha linearly over the remaining lifetime.
        p.color.a = glm::clamp(p.life / p.maxLife, 0.f, 1.f);
    }
}

void ParticleSystem::emit(Effect effect, const glm::vec3& pos, const glm::vec3& dir, int count) {
    glm::vec3 baseDir = dir;
    if (glm::length(baseDir) < 1e-4f)
        baseDir = {0.f, 1.f, 0.f};
    else
        baseDir = glm::normalize(baseDir);

    // Per-effect tuning. spread = how far velocity jitters off baseDir,
    // speed = base speed along the jittered direction.
    Blend blend = Blend::Alpha;
    glm::vec3 color{1.f};
    float speed   = 4.f;
    float spread  = 0.5f;
    float size    = 0.1f;
    float life    = 0.5f;
    float gravity = 0.f;

    switch (effect) {
    case Effect::MuzzleFlash:
        blend   = Blend::Additive;
        color   = {1.f, 0.85f, 0.4f};
        speed   = 2.5f;
        spread  = 0.35f;
        size    = 0.18f;
        life    = 0.12f;
        gravity = 0.f;
        break;
    case Effect::BloodBurst:
        blend   = Blend::Alpha;
        color   = {0.6f, 0.04f, 0.04f};
        speed   = 3.5f;
        spread  = 0.8f;
        size    = 0.12f;
        life    = 0.6f;
        gravity = 9.81f;
        break;
    case Effect::ImpactSparks:
        blend   = Blend::Additive;
        color   = {1.f, 0.6f, 0.2f};
        speed   = 6.f;
        spread  = 0.9f;
        size    = 0.06f;
        life    = 0.35f;
        gravity = 6.f;
        break;
    case Effect::Explosion:
        blend   = Blend::Additive;
        color   = {1.f, 0.5f, 0.15f};
        speed   = 7.f;
        spread  = 1.f;
        size    = 0.35f;
        life    = 0.7f;
        gravity = 1.5f;
        break;
    }

    for (int n = 0; n < count; ++n) {
        std::size_t idx = allocate();
        if (idx == kInvalid)
            return; // pool exhausted

        Particle& p      = m_particles[idx];
        glm::vec3 jitter = glm::normalize(baseDir + randInSphere() * spread);
        float spd        = speed * (0.6f + 0.4f * randUnit());

        p.position = pos;
        p.velocity = jitter * spd;
        p.maxLife  = life * (0.7f + 0.6f * randUnit());
        p.life     = p.maxLife;
        p.size     = size * (0.7f + 0.6f * randUnit());
        p.color    = glm::vec4(color, 1.f);
        p.gravity  = gravity;
        p.blend    = blend;
        p.alive    = true;
        ++m_aliveCount;
    }
}

void ParticleSystem::buildInstances() {
    m_alphaInstances.clear();
    m_additiveInstances.clear();

    for (const Particle& p : m_particles) {
        if (!p.alive)
            continue;
        Instance inst{p.position, p.size, p.color};
        if (p.blend == Blend::Additive)
            m_additiveInstances.push_back(inst);
        else
            m_alphaInstances.push_back(inst);
    }
}

void ParticleSystem::buildGpuParticles() {
    m_gpuParticles.clear();
    m_gpuAlphaCount = 0;

    // Alpha bucket first so the dispatch output's prefix is the alpha draw range
    // and the suffix is the additive draw range (see renderParticlesCompute).
    for (const Particle& p : m_particles) {
        if (p.alive && p.blend == Blend::Alpha) {
            m_gpuParticles.push_back(GpuParticle{p.position, p.size, p.color, p.velocity, p.life});
            ++m_gpuAlphaCount;
        }
    }
    for (const Particle& p : m_particles) {
        if (p.alive && p.blend == Blend::Additive)
            m_gpuParticles.push_back(GpuParticle{p.position, p.size, p.color, p.velocity, p.life});
    }
}

} // namespace ds
