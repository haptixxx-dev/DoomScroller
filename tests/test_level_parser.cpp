#include "engine/LevelTextParser.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>

using namespace ds;

// Exercises the pure text-level parser. Links engine_headers only: LevelData and
// its records are POD (entt is header-only and unused here), no SDL3/Jolt.

TEST_CASE("parseLevelText parses boxes, spawns and lights with correct values", "[levelparser]") {
    constexpr std::string_view text = "# a tiny level\n"
                                      "\n"
                                      "box   0 -0.1 0   10 0.1 10   1 1 1\n"
                                      "box   0 2.5 -10  10 2.5 0.1   0.5 0.25 0.75\n"
                                      "spawn 0 1.7 0   player\n"
                                      "spawn -7 1.5 -7  enemy\n"
                                      "light 0 4.5 0   1 0.9 0.8   20 1.5\n";

    std::string err;
    std::optional<LevelData> level = parseLevelText(text, &err);
    REQUIRE(level.has_value());
    REQUIRE(err.empty());

    REQUIRE(level->boxes.size() == 2);
    REQUIRE(level->spawns.size() == 2);
    REQUIRE(level->lights.size() == 1);

    // Header counts mirror the vector sizes.
    REQUIRE(level->header.boxCount == 2u);
    REQUIRE(level->header.spawnCount == 2u);
    REQUIRE(level->header.lightCount == 1u);

    // Second box values.
    REQUIRE(level->boxes[1].center[1] == Catch::Approx(2.5f));
    REQUIRE(level->boxes[1].center[2] == Catch::Approx(-10.f));
    REQUIRE(level->boxes[1].halfExtents[2] == Catch::Approx(0.1f));
    REQUIRE(level->boxes[1].color[0] == Catch::Approx(0.5f));
    REQUIRE(level->boxes[1].color[1] == Catch::Approx(0.25f));
    REQUIRE(level->boxes[1].color[2] == Catch::Approx(0.75f));
    REQUIRE(level->boxes[1].materialRef == 0u);

    // Light values.
    REQUIRE(level->lights[0].position[1] == Catch::Approx(4.5f));
    REQUIRE(level->lights[0].color[1] == Catch::Approx(0.9f));
    REQUIRE(level->lights[0].radius == Catch::Approx(20.f));
    REQUIRE(level->lights[0].intensity == Catch::Approx(1.5f));
}

TEST_CASE("parseLevelText sets player flag bit0, leaves enemy flags zero", "[levelparser]") {
    constexpr std::string_view text = "spawn 1 2 3 player\n"
                                      "spawn 4 5 6 enemy\n";
    std::optional<LevelData> level  = parseLevelText(text);
    REQUIRE(level.has_value());
    REQUIRE(level->spawns.size() == 2);

    REQUIRE((level->spawns[0].flags & 1u) == 1u);
    REQUIRE(level->spawns[0].position[0] == Catch::Approx(1.f));

    REQUIRE(level->spawns[1].flags == 0u);
    REQUIRE(level->spawns[1].position[2] == Catch::Approx(6.f));
}

TEST_CASE("parseLevelText skips comments and blank lines", "[levelparser]") {
    constexpr std::string_view text = "\n"
                                      "   \t  \n"
                                      "# full-line comment\n"
                                      "box 0 0 0 1 1 1 1 1 1   # trailing comment\n"
                                      "\n";
    std::string err;
    std::optional<LevelData> level = parseLevelText(text, &err);
    REQUIRE(level.has_value());
    REQUIRE(level->boxes.size() == 1);
    REQUIRE(level->spawns.empty());
    REQUIRE(level->lights.empty());
}

TEST_CASE("parseLevelText fails on too few tokens", "[levelparser]") {
    constexpr std::string_view text = "box 0 0 0 1 1 1\n"; // missing r g b
    std::string err;
    std::optional<LevelData> level = parseLevelText(text, &err);
    REQUIRE_FALSE(level.has_value());
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("parseLevelText fails on non-numeric value", "[levelparser]") {
    constexpr std::string_view text = "light 0 0 0 1 1 1 abc 1\n";
    std::string err;
    std::optional<LevelData> level = parseLevelText(text, &err);
    REQUIRE_FALSE(level.has_value());
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("parseLevelText fails on unknown record kind", "[levelparser]") {
    constexpr std::string_view text = "teleporter 0 0 0\n";
    std::string err;
    std::optional<LevelData> level = parseLevelText(text, &err);
    REQUIRE_FALSE(level.has_value());
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("parseLevelText fails on bad spawn kind", "[levelparser]") {
    constexpr std::string_view text = "spawn 0 0 0 boss\n";
    std::optional<LevelData> level  = parseLevelText(text);
    REQUIRE_FALSE(level.has_value());
}

TEST_CASE("parseLevelText on empty input yields an empty level", "[levelparser]") {
    std::optional<LevelData> level = parseLevelText("");
    REQUIRE(level.has_value());
    REQUIRE(level->boxes.empty());
    REQUIRE(level->spawns.empty());
    REQUIRE(level->lights.empty());
}

TEST_CASE("parseLevelText encodes an enemy archetype hint in flags bits 1-2", "[levelparser]") {
    // Archetype value (grunt=0, charger=1, ranged=2) is stored as (value+1) << 1.
    constexpr std::string_view text = "spawn 1 2 3 enemy grunt\n"
                                      "spawn 4 5 6 enemy charger\n"
                                      "spawn 7 8 9 enemy ranged\n";
    std::optional<LevelData> level  = parseLevelText(text);
    REQUIRE(level.has_value());
    REQUIRE(level->spawns.size() == 3);

    REQUIRE(level->spawns[0].flags == ((0u + 1u) << 1u)); // grunt   -> 2
    REQUIRE(level->spawns[1].flags == ((1u + 1u) << 1u)); // charger -> 4
    REQUIRE(level->spawns[2].flags == ((2u + 1u) << 1u)); // ranged  -> 6

    // bit0 (player start) stays clear on all enemy spawns.
    for (const auto& s : level->spawns) {
        REQUIRE((s.flags & 1u) == 0u);
    }
}

TEST_CASE("parseLevelText keeps 5-token enemy/player spawns back-compatible", "[levelparser]") {
    // No archetype token: enemy flags 0, player flags bit0 (unchanged behavior).
    constexpr std::string_view text = "spawn 0 0 0 enemy\n"
                                      "spawn 1 1 1 player\n";
    std::optional<LevelData> level  = parseLevelText(text);
    REQUIRE(level.has_value());
    REQUIRE(level->spawns.size() == 2);
    REQUIRE(level->spawns[0].flags == 0u);
    REQUIRE((level->spawns[1].flags & 1u) == 1u);
}

TEST_CASE("parseLevelText rejects an archetype token on a player spawn", "[levelparser]") {
    constexpr std::string_view text = "spawn 1 2 3 player charger\n";
    std::string err;
    std::optional<LevelData> level = parseLevelText(text, &err);
    REQUIRE_FALSE(level.has_value());
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("parseLevelText rejects an unknown archetype token", "[levelparser]") {
    constexpr std::string_view text = "spawn 1 2 3 enemy bogus\n";
    std::string err;
    std::optional<LevelData> level = parseLevelText(text, &err);
    REQUIRE_FALSE(level.has_value());
    REQUIRE_FALSE(err.empty());
}
