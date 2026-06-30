#include "engine/BossLogic.h"
#include "engine/ecs/Components.h"

#include <array>
#include <catch2/catch_test_macros.hpp>

using namespace ds;

TEST_CASE("bossPhaseForHealth advances as health crosses descending thresholds", "[boss]") {
    std::array<float, 3> thresholds{0.66f, 0.33f, 0.0f};
    std::span<const float> t{thresholds};

    REQUIRE(bossPhaseForHealth(2000, 2000, t) == 0); // full health, phase 0
    REQUIRE(bossPhaseForHealth(1400, 2000, t) == 0); // 70% still phase 0
    REQUIRE(bossPhaseForHealth(1320, 2000, t) == 1); // exactly 66% -> phase 1
    REQUIRE(bossPhaseForHealth(1000, 2000, t) == 1); // 50% phase 1
    REQUIRE(bossPhaseForHealth(660, 2000, t) == 2);  // 33% -> phase 2
    REQUIRE(bossPhaseForHealth(200, 2000, t) == 2);  // 10% phase 2
    REQUIRE(bossPhaseForHealth(0, 2000, t) == 3);    // dead -> final phase
}

TEST_CASE("bossPhaseForHealth clamps negative health to the final phase", "[boss]") {
    std::array<float, 3> thresholds{0.66f, 0.33f, 0.0f};
    REQUIRE(bossPhaseForHealth(-50, 2000, std::span<const float>{thresholds}) == 3);
}

TEST_CASE("bossPhaseForHealth handles zero/negative max defensively", "[boss]") {
    std::array<float, 2> thresholds{0.5f, 0.0f};
    REQUIRE(bossPhaseForHealth(100, 0, std::span<const float>{thresholds}) == 2);
}

TEST_CASE("bossPhaseForHealth never advances backward as health drops", "[boss]") {
    std::array<float, 3> thresholds{0.66f, 0.33f, 0.0f};
    std::span<const float> t{thresholds};
    int prev = 0;
    for (int hp = 2000; hp >= 0; hp -= 50) {
        int phase = bossPhaseForHealth(hp, 2000, t);
        REQUIRE(phase >= prev);
        prev = phase;
    }
}

TEST_CASE("BossComponent defaults", "[boss]") {
    BossComponent b;
    REQUIRE(b.phase == 0);
    REQUIRE(b.maxHealth == 2000);
    REQUIRE(b.attackTimer == 0.f);
    REQUIRE(b.phaseHealthThresholds[0] == 0.66f);
}
