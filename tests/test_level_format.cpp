#include "engine/LevelFormat.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

using namespace ds;

// These tests exercise the on-disk binary layout of the level format directly,
// without linking the full engine (no SDL3/Jolt). They mirror the byte layout
// that LevelLoader::read / ::write use.

TEST_CASE("Level record sizes match the documented on-disk layout", "[level]") {
    REQUIRE(sizeof(LevelHeader) == 32);
    REQUIRE(sizeof(BoxRecord) == 40);
    REQUIRE(sizeof(SpawnPointRecord) == 16);
    REQUIRE(sizeof(LightRecord) == 32);
}

TEST_CASE("Magic spells 'DSLV' little-endian", "[level]") {
    const auto* bytes = reinterpret_cast<const char*>(&kLevelMagic);
    REQUIRE(bytes[0] == 'D');
    REQUIRE(bytes[1] == 'S');
    REQUIRE(bytes[2] == 'L');
    REQUIRE(bytes[3] == 'V');
}

TEST_CASE("LevelHeader defaults carry correct magic and version", "[level]") {
    LevelHeader h{};
    REQUIRE(h.magic == kLevelMagic);
    REQUIRE(h.version == kLevelVersion);
    REQUIRE(h.boxCount == 0);
    REQUIRE(h.spawnCount == 0);
    REQUIRE(h.lightCount == 0);
}

TEST_CASE("Header round-trips through a binary file", "[level]") {
    auto path = std::filesystem::temp_directory_path() / "ds_level_header_roundtrip.dslv";

    LevelHeader out{};
    out.boxCount   = 6;
    out.spawnCount = 4;
    out.lightCount = 1;

    {
        std::FILE* f = std::fopen(path.string().c_str(), "wb");
        REQUIRE(f != nullptr);
        REQUIRE(std::fwrite(&out, sizeof(out), 1, f) == 1);
        std::fclose(f);
    }

    LevelHeader in{};
    in.magic = 0; // clobber defaults so the read must repopulate them
    {
        std::FILE* f = std::fopen(path.string().c_str(), "rb");
        REQUIRE(f != nullptr);
        REQUIRE(std::fread(&in, sizeof(in), 1, f) == 1);
        std::fclose(f);
    }

    REQUIRE(in.magic == kLevelMagic);
    REQUIRE(in.version == kLevelVersion);
    REQUIRE(in.boxCount == 6);
    REQUIRE(in.spawnCount == 4);
    REQUIRE(in.lightCount == 1);

    std::filesystem::remove(path);
}

TEST_CASE("Version mismatch is detectable by the reader contract", "[level]") {
    LevelHeader h{};
    h.version = kLevelVersion + 1;
    // A reader must reject this; we assert the comparison the loader performs.
    REQUIRE_FALSE(h.version == kLevelVersion);
    REQUIRE(h.magic == kLevelMagic);
}

TEST_CASE("Full level body round-trips (header + all record blocks)", "[level]") {
    auto path = std::filesystem::temp_directory_path() / "ds_level_body_roundtrip.dslv";

    std::vector<BoxRecord> boxes(2);
    boxes[0].center[0]      = 1.f;
    boxes[0].halfExtents[1] = 2.5f;
    boxes[0].color[2]       = 0.5f;
    boxes[0].materialRef    = 7;
    boxes[1].center[2]      = -3.f;

    std::vector<SpawnPointRecord> spawns(1);
    spawns[0].position[1] = 1.7f;
    spawns[0].flags       = 1u;

    std::vector<LightRecord> lights(1);
    lights[0].radius    = 12.5f;
    lights[0].intensity = 0.75f;

    LevelHeader header{};
    header.boxCount   = static_cast<uint32_t>(boxes.size());
    header.spawnCount = static_cast<uint32_t>(spawns.size());
    header.lightCount = static_cast<uint32_t>(lights.size());

    {
        std::FILE* f = std::fopen(path.string().c_str(), "wb");
        REQUIRE(f != nullptr);
        REQUIRE(std::fwrite(&header, sizeof(header), 1, f) == 1);
        REQUIRE(std::fwrite(boxes.data(), sizeof(BoxRecord), boxes.size(), f) == boxes.size());
        REQUIRE(std::fwrite(spawns.data(), sizeof(SpawnPointRecord), spawns.size(), f) == spawns.size());
        REQUIRE(std::fwrite(lights.data(), sizeof(LightRecord), lights.size(), f) == lights.size());
        std::fclose(f);
    }

    LevelHeader rh{};
    std::vector<BoxRecord> rb;
    std::vector<SpawnPointRecord> rs;
    std::vector<LightRecord> rl;
    {
        std::FILE* f = std::fopen(path.string().c_str(), "rb");
        REQUIRE(f != nullptr);
        REQUIRE(std::fread(&rh, sizeof(rh), 1, f) == 1);
        rb.resize(rh.boxCount);
        rs.resize(rh.spawnCount);
        rl.resize(rh.lightCount);
        REQUIRE(std::fread(rb.data(), sizeof(BoxRecord), rh.boxCount, f) == rh.boxCount);
        REQUIRE(std::fread(rs.data(), sizeof(SpawnPointRecord), rh.spawnCount, f) == rh.spawnCount);
        REQUIRE(std::fread(rl.data(), sizeof(LightRecord), rh.lightCount, f) == rh.lightCount);
        std::fclose(f);
    }

    REQUIRE(rh.boxCount == 2);
    REQUIRE(rb[0].center[0] == 1.f);
    REQUIRE(rb[0].halfExtents[1] == 2.5f);
    REQUIRE(rb[0].color[2] == 0.5f);
    REQUIRE(rb[0].materialRef == 7);
    REQUIRE(rb[1].center[2] == -3.f);

    REQUIRE(rh.spawnCount == 1);
    REQUIRE(rs[0].position[1] == 1.7f);
    REQUIRE(rs[0].flags == 1u);

    REQUIRE(rh.lightCount == 1);
    REQUIRE(rl[0].radius == 12.5f);
    REQUIRE(rl[0].intensity == 0.75f);

    std::filesystem::remove(path);
}
