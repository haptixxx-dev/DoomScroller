#include "engine/ParticleSystem.h"

#include <catch2/catch_test_macros.hpp>

using ds::ParticleSystem;

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
