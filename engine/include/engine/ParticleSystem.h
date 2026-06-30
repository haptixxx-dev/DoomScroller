#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace ds {

// CPU particle simulation with a fixed-capacity pool and a free list.
//
// Particles are simulated on the CPU (integrate velocity + gravity, age, fade)
// and rendered as camera-facing billboards via GPU instancing. The renderer
// reads alive particles each frame through aliveInstances(); particles are
// split into two blend buckets so the engine can issue one additive pass and
// one alpha-blended pass.
class ParticleSystem {
  public:
    static constexpr std::size_t kMaxParticles = 4096;

    enum class Blend : uint8_t { Alpha, Additive };

    // Named emitter presets. Each preset picks a color/size/speed/lifetime
    // profile and a blend mode (see emit()).
    enum class Effect : uint8_t {
        MuzzleFlash,  // additive yellow-white burst at the gun
        BloodBurst,   // alpha red spray when an enemy is hit
        ImpactSparks, // additive orange sparks on a wall hit
        Explosion,    // additive fireball (reserved for task 17)
    };

    // GPU per-instance data: world position, billboard size, and rgba color.
    // Layout must match the instanced vertex bindings in particle.slang.
    struct Instance {
        glm::vec3 position{0.f};
        float size = 0.f;
        glm::vec4 color{0.f};
    };

    // Per-particle state uploaded to the GPU compute sim (task 39). Layout must
    // match `GpuParticle` in particle_sim.slang (std430): position+size,
    // color, velocity+life. Dead particles carry life<=0 so the shader emits a
    // zero-size instance for them.
    struct GpuParticle {
        glm::vec3 position{0.f};
        float size = 0.f;
        glm::vec4 color{0.f};
        glm::vec3 velocity{0.f};
        float life = 0.f;
    };

    // Snapshot the alive particles into a flat GpuParticle array for the compute
    // path, ALPHA-blend particles first then ADDITIVE. Index i in the array maps
    // to instance i in the dispatch output, so the alpha bucket is the prefix
    // [0, gpuAlphaCount()) and the additive bucket is the suffix. Reused across
    // frames (cleared, not realloced).
    const std::vector<GpuParticle>& gpuParticles() const { return m_gpuParticles; }
    std::size_t gpuAlphaCount() const { return m_gpuAlphaCount; }
    void buildGpuParticles();

    ParticleSystem();

    // Advance the simulation: integrate motion + gravity, age particles, fade
    // their alpha toward zero over their lifetime, and recycle dead particles.
    void update(float dt);

    // Spawn `count` particles for the given preset at `pos`, biased along `dir`
    // (typically a surface normal or the shot direction). Silently drops
    // particles when the pool is full.
    void emit(Effect effect, const glm::vec3& pos, const glm::vec3& dir, int count);

    // Number of currently alive particles.
    std::size_t aliveCount() const { return m_aliveCount; }

    // Build the per-instance buffers for this frame, one per blend bucket, in
    // arbitrary order. The vectors are owned by the system and reused across
    // frames. Call after update().
    void buildInstances();

    const std::vector<Instance>& alphaInstances() const { return m_alphaInstances; }
    const std::vector<Instance>& additiveInstances() const { return m_additiveInstances; }

  private:
    struct Particle {
        glm::vec3 position{0.f};
        glm::vec3 velocity{0.f};
        glm::vec4 color{0.f};
        float size    = 0.f;
        float life    = 0.f; // remaining seconds
        float maxLife = 1.f;
        float gravity = 0.f; // m/s^2 applied along -Y
        bool alive    = false;
        Blend blend   = Blend::Alpha;
    };

    // Pull a free slot off the free list, or kInvalid if the pool is full.
    std::size_t allocate();

    static constexpr std::size_t kInvalid = static_cast<std::size_t>(-1);

    std::array<Particle, kMaxParticles> m_particles{};
    std::vector<std::size_t> m_freeList; // indices available for reuse
    std::size_t m_aliveCount = 0;
    uint32_t m_rngState      = 0x12345678u;

    std::vector<Instance> m_alphaInstances;
    std::vector<Instance> m_additiveInstances;
    std::vector<GpuParticle> m_gpuParticles;
    std::size_t m_gpuAlphaCount = 0;

    float randUnit();         // [0,1)
    glm::vec3 randInSphere(); // unit-ball vector
};

} // namespace ds
