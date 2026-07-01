#include "engine/QualityProfile.h"
#include "engine/rhi/RHITypes.h"

#include <catch2/catch_test_macros.hpp>

using namespace ds;

TEST_CASE("8GB discrete GPU selects Enhanced with full profile", "[quality]") {
    ds::rhi::RHICaps caps{};
    caps.deviceVRAMBytes = 8ull * 1024ull * 1024ull * 1024ull;

    REQUIRE(selectTier(caps) == QualityTier::Enhanced);

    QualityProfile p = profileForCaps(caps);
    REQUIRE(p.tier == QualityTier::Enhanced);
    REQUIRE(p.shadows == true);
    REQUIRE(p.bloom == true);
    REQUIRE(p.halfResBloom == false);
    REQUIRE(p.computeParticles == true);
    REQUIRE(p.shadowMapSize == 2048);
    REQUIRE(p.renderScale == 1.f);
}

TEST_CASE("3GB GPU without mesh shaders selects Minimum", "[quality]") {
    ds::rhi::RHICaps caps{};
    caps.deviceVRAMBytes = 3ull * 1024ull * 1024ull * 1024ull;
    caps.meshShaders     = false;
    caps.bindless        = false;

    REQUIRE(selectTier(caps) == QualityTier::Minimum);

    QualityProfile p = profileForCaps(caps);
    REQUIRE(p.tier == QualityTier::Minimum);
    REQUIRE(p.shadows == false);
    REQUIRE(p.bloom == true);
    REQUIRE(p.halfResBloom == true);
    REQUIRE(p.computeParticles == false);
    REQUIRE(p.shadowMapSize == 1024);
    REQUIRE(p.renderScale == 1.f);
}

TEST_CASE("Unknown VRAM (0) is conservatively Minimum", "[quality]") {
    ds::rhi::RHICaps caps{};
    caps.deviceVRAMBytes = 0;

    REQUIRE(selectTier(caps) == QualityTier::Minimum);

    QualityProfile p = profileForCaps(caps);
    REQUIRE(p.tier == QualityTier::Minimum);
    REQUIRE(p.shadows == false);
    REQUIRE(p.shadowMapSize == 1024);
}

TEST_CASE("Mesh shaders force Enhanced even on low reported VRAM", "[quality]") {
    ds::rhi::RHICaps caps{};
    caps.deviceVRAMBytes = 2ull * 1024ull * 1024ull * 1024ull;
    caps.meshShaders     = true;

    REQUIRE(selectTier(caps) == QualityTier::Enhanced);
    REQUIRE(profileForCaps(caps).shadows == true);
    REQUIRE(profileForCaps(caps).shadowMapSize == 2048);
}

TEST_CASE("Bindless forces Enhanced even on low reported VRAM", "[quality]") {
    ds::rhi::RHICaps caps{};
    caps.deviceVRAMBytes = 1ull * 1024ull * 1024ull * 1024ull;
    caps.bindless        = true;

    REQUIRE(selectTier(caps) == QualityTier::Enhanced);
    REQUIRE(profileForCaps(caps).computeParticles == true);
}

TEST_CASE("Exactly 6GiB threshold selects Enhanced", "[quality]") {
    ds::rhi::RHICaps caps{};
    caps.deviceVRAMBytes = kEnhancedVRAMThreshold;

    REQUIRE(selectTier(caps) == QualityTier::Enhanced);
}

TEST_CASE("profileForTier is a pure tier lookup", "[quality]") {
    QualityProfile mn = profileForTier(QualityTier::Minimum);
    REQUIRE(mn.tier == QualityTier::Minimum);
    REQUIRE(mn.shadows == false);
    REQUIRE(mn.halfResBloom == true);
    REQUIRE(mn.shadowMapSize == 1024);

    QualityProfile en = profileForTier(QualityTier::Enhanced);
    REQUIRE(en.tier == QualityTier::Enhanced);
    REQUIRE(en.shadows == true);
    REQUIRE(en.halfResBloom == false);
    REQUIRE(en.shadowMapSize == 2048);
}

TEST_CASE("QualityProfile defaults match Minimum preset", "[quality]") {
    QualityProfile p{};
    REQUIRE(p.tier == QualityTier::Minimum);
    REQUIRE(p.shadows == false);
    REQUIRE(p.bloom == true);
    REQUIRE(p.halfResBloom == true);
    REQUIRE(p.computeParticles == false);
    REQUIRE(p.shadowMapSize == 1024);
    REQUIRE(p.renderScale == 1.f);
}

TEST_CASE("capsFromRawQuery zeroes implausible VRAM", "[quality]") {
    ds::rhi::RHICaps raw{};
    raw.maxTextureDim   = 16384; // a real device populated the query
    raw.deviceVRAMBytes = UINT64_MAX;
    ds::rhi::RHICaps s  = capsFromRawQuery(raw);
    REQUIRE(s.deviceVRAMBytes == 0u); // implausible -> unknown, cannot force Enhanced
    REQUIRE(selectTier(s) == QualityTier::Minimum);
}

TEST_CASE("capsFromRawQuery distrusts a query that didn't populate", "[quality]") {
    // maxTextureDim == 0 means the query failed to fill caps; feature bits and
    // VRAM must not be trusted, so we fall to a conservative Minimum.
    ds::rhi::RHICaps raw{};
    raw.maxTextureDim   = 0;
    raw.meshShaders     = true; // stale/garbage
    raw.bindless        = true;
    raw.deviceVRAMBytes = 8ull * 1024ull * 1024ull * 1024ull;
    ds::rhi::RHICaps s  = capsFromRawQuery(raw);
    REQUIRE(s.meshShaders == false);
    REQUIRE(s.bindless == false);
    REQUIRE(s.deviceVRAMBytes == 0u);
    REQUIRE(selectTier(s) == QualityTier::Minimum);
}

TEST_CASE("capsFromRawQuery passes valid caps through unchanged", "[quality]") {
    ds::rhi::RHICaps raw{};
    raw.maxTextureDim   = 16384;
    raw.meshShaders     = true;
    raw.deviceVRAMBytes = 8ull * 1024ull * 1024ull * 1024ull;
    ds::rhi::RHICaps s  = capsFromRawQuery(raw);
    REQUIRE(s.meshShaders == true);
    REQUIRE(s.deviceVRAMBytes == raw.deviceVRAMBytes);
    REQUIRE(selectTier(s) == QualityTier::Enhanced);
}

TEST_CASE("useMeshShaders requires Enhanced tier AND the mesh-shader cap", "[quality]") {
    // Enhanced by VRAM alone, but the device lacks mesh shaders -> classic path.
    ds::rhi::RHICaps vramOnly{};
    vramOnly.deviceVRAMBytes     = 8ull * 1024ull * 1024ull * 1024ull;
    const QualityProfile enhVram = profileForCaps(vramOnly);
    REQUIRE(enhVram.tier == QualityTier::Enhanced);
    REQUIRE_FALSE(useMeshShaders(enhVram, vramOnly));

    // Enhanced AND mesh-shader capable -> mesh path on.
    ds::rhi::RHICaps mesh{};
    mesh.meshShaders           = true;
    const QualityProfile enhMs = profileForCaps(mesh);
    REQUIRE(enhMs.tier == QualityTier::Enhanced);
    REQUIRE(useMeshShaders(enhMs, mesh));

    // A Minimum-tier device never uses mesh shaders even if the bit is set
    // (defensive: the profile gates it).
    ds::rhi::RHICaps lowMesh{};
    lowMesh.meshShaders = true;
    REQUIRE_FALSE(useMeshShaders(profileForTier(QualityTier::Minimum), lowMesh));
}
