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
