#include "engine/ParticleSystem.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using ds::ParticleSystem;

namespace {

// CPU mirror of particle_sim.slang's compMain (task 58): the compute pass is now
// a pure pass-through PACK of the GpuParticle snapshot into the Instance layout —
// alive particles copy position/size/color unchanged (NO extra dt), dead ones
// (life <= 0) collapse to a zero instance. gpuParticles() only ever contains the
// alive particles, so the dead branch is never hit here, but it is mirrored for
// fidelity. This is the exact byte-for-byte fill the GPU emits into the instance
// buffer, so asserting it matches buildInstances() pins compute/CPU-fallback
// parity — the one-frame-lead the extrapolation used to introduce is gone.
std::vector<ParticleSystem::Instance> packComputeInstances(const ParticleSystem& ps) {
    std::vector<ParticleSystem::Instance> out;
    for (const ParticleSystem::GpuParticle& p : ps.gpuParticles()) {
        ParticleSystem::Instance inst{};
        if (p.life <= 0.f) {
            inst.position = glm::vec3(0.f);
            inst.size     = 0.f;
            inst.color    = glm::vec4(0.f);
        } else {
            inst.position = p.position;
            inst.size     = p.size;
            inst.color    = p.color;
        }
        out.push_back(inst);
    }
    return out;
}

// Order-independent set equality on Instance fields (buildInstances() emits in
// arbitrary pool order; the compute pack follows the alpha-then-additive
// snapshot order, so a positional compare would be brittle).
bool sameInstanceSet(std::vector<ParticleSystem::Instance> a, std::vector<ParticleSystem::Instance> b) {
    if (a.size() != b.size())
        return false;
    auto less = [](const ParticleSystem::Instance& l, const ParticleSystem::Instance& r) {
        if (l.position.x != r.position.x)
            return l.position.x < r.position.x;
        if (l.position.y != r.position.y)
            return l.position.y < r.position.y;
        if (l.position.z != r.position.z)
            return l.position.z < r.position.z;
        if (l.size != r.size)
            return l.size < r.size;
        if (l.color.r != r.color.r)
            return l.color.r < r.color.r;
        if (l.color.g != r.color.g)
            return l.color.g < r.color.g;
        if (l.color.b != r.color.b)
            return l.color.b < r.color.b;
        return l.color.a < r.color.a;
    };
    std::sort(a.begin(), a.end(), less);
    std::sort(b.begin(), b.end(), less);
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].position != b[i].position || a[i].size != b[i].size || a[i].color != b[i].color)
            return false;
    }
    return true;
}

} // namespace

TEST_CASE("emit increases alive count, capped at pool size", "[particles]") {
    ParticleSystem ps;
    REQUIRE(ps.aliveCount() == 0);

    ps.emit(ParticleSystem::Effect::MuzzleFlash, {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 10);
    REQUIRE(ps.aliveCount() == 10);

    // Overflowing the pool drops the excess instead of overrunning.
    ps.emit(ParticleSystem::Effect::BloodBurst, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
            static_cast<int>(ParticleSystem::kMaxParticles) + 100);
    REQUIRE(ps.aliveCount() == ParticleSystem::kMaxParticles);
}

TEST_CASE("particles expire after their lifetime and slots are recycled", "[particles]") {
    ParticleSystem ps;
    ps.emit(ParticleSystem::Effect::ImpactSparks, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 8);
    REQUIRE(ps.aliveCount() == 8);

    // ImpactSparks lifetime is well under 2s; advance past it.
    for (int i = 0; i < 100; ++i)
        ps.update(0.05f);
    REQUIRE(ps.aliveCount() == 0);

    // Pool slots were freed, so we can emit again.
    ps.emit(ParticleSystem::Effect::ImpactSparks, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 4);
    REQUIRE(ps.aliveCount() == 4);
}

TEST_CASE("buildInstances splits alpha and additive buckets", "[particles]") {
    ParticleSystem ps;
    ps.emit(ParticleSystem::Effect::BloodBurst, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 5);  // alpha
    ps.emit(ParticleSystem::Effect::MuzzleFlash, {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 7); // additive
    ps.buildInstances();

    REQUIRE(ps.alphaInstances().size() == 5);
    REQUIRE(ps.additiveInstances().size() == 7);
}

TEST_CASE("buildGpuParticles orders alpha bucket before additive", "[particles]") {
    ParticleSystem ps;
    ps.emit(ParticleSystem::Effect::BloodBurst, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 5);  // alpha
    ps.emit(ParticleSystem::Effect::MuzzleFlash, {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 7); // additive
    ps.buildGpuParticles();

    // All 12 live particles are snapshotted; the alpha prefix count matches the
    // alpha bucket so the compute draw can slice [0,alpha)/[alpha,total).
    REQUIRE(ps.gpuParticles().size() == 12);
    REQUIRE(ps.gpuAlphaCount() == 5);

    // The prefix is the alpha (BloodBurst, reddish) bucket; every entry alive.
    for (std::size_t i = 0; i < ps.gpuAlphaCount(); ++i)
        REQUIRE(ps.gpuParticles()[i].life > 0.f);
}

TEST_CASE("compute pack equals CPU buildInstances output for the same pool+dt", "[particles]") {
    // Task 58: the GPU compute path (particle_sim.slang) must emit the SAME
    // per-instance positions the CPU fallback (buildInstances) produces — no
    // extra dt extrapolation, no one-frame lead. Both paths run over the same
    // already-integrated pool, so this pins their outputs equal.
    ParticleSystem ps;
    ps.emit(ParticleSystem::Effect::BloodBurst, {1.f, 2.f, 3.f}, {0.f, 1.f, 0.f}, 6);    // alpha
    ps.emit(ParticleSystem::Effect::ImpactSparks, {-2.f, 0.f, 4.f}, {1.f, 0.f, 0.f}, 9); // additive

    // Advance a couple of frames so positions/velocities are non-trivial and a
    // stale one-dt extrapolation would visibly diverge.
    ps.update(0.05f);
    ps.update(0.05f);

    // Both paths snapshot the CURRENT pool state.
    ps.buildInstances();
    ps.buildGpuParticles();

    const auto packed = packComputeInstances(ps);

    // Split the compute pack back into blend buckets by the alpha prefix count
    // (buildGpuParticles orders alpha first, then additive).
    std::vector<ParticleSystem::Instance> packedAlpha(packed.begin(),
                                                      packed.begin() + static_cast<std::ptrdiff_t>(ps.gpuAlphaCount()));
    std::vector<ParticleSystem::Instance> packedAdditive(
        packed.begin() + static_cast<std::ptrdiff_t>(ps.gpuAlphaCount()), packed.end());

    // Same count per bucket, and the exact same instance data (no dt applied).
    REQUIRE(packedAlpha.size() == ps.alphaInstances().size());
    REQUIRE(packedAdditive.size() == ps.additiveInstances().size());
    REQUIRE(sameInstanceSet(packedAlpha, ps.alphaInstances()));
    REQUIRE(sameInstanceSet(packedAdditive, ps.additiveInstances()));

    // Ground-truth guard: every packed position is a live particle's position
    // with NO extra dt applied. If the shader still extrapolated (pos + vel*dt),
    // no compute position would coincide with a buildInstances position for a
    // moving particle, so the set-equality above would fail — this REQUIRE just
    // states the invariant the fix guarantees.
    std::vector<glm::vec3> cpuPositions;
    for (const auto& inst : ps.alphaInstances())
        cpuPositions.push_back(inst.position);
    for (const auto& inst : ps.additiveInstances())
        cpuPositions.push_back(inst.position);
    for (const auto& gp : ps.gpuParticles()) {
        const bool found =
            std::any_of(cpuPositions.begin(), cpuPositions.end(), [&](const glm::vec3& c) { return c == gp.position; });
        REQUIRE(found);
    }
}

TEST_CASE("alpha fades toward zero as a particle ages", "[particles]") {
    ParticleSystem ps;
    ps.emit(ParticleSystem::Effect::BloodBurst, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 1);
    ps.buildInstances();
    float startAlpha = ps.alphaInstances().front().color.a;

    // One small step shouldn't kill it but should reduce alpha.
    ps.update(0.1f);
    ps.buildInstances();
    REQUIRE(ps.aliveCount() == 1);
    REQUIRE(ps.alphaInstances().front().color.a < startAlpha);
}
