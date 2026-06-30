#include "engine/UserStorage.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace ds;

namespace {

// Fixed (non-random, non-time-based) temp directory so the test is fully
// deterministic. Cleaned up at the end of each case via the RAII guard below.
std::filesystem::path testDir() {
    return std::filesystem::temp_directory_path() / "ds_userstorage_test";
}

// Removes the test directory tree on construction and destruction so each case
// starts clean and leaves nothing behind, even if an assertion aborts midway.
struct ScopedTestDir {
    std::filesystem::path dir = testDir();
    ScopedTestDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    ~ScopedTestDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

} // namespace

TEST_CASE("writeFile/readFile round-trips raw bytes", "[userstorage]") {
    ScopedTestDir guard;
    const std::filesystem::path file = guard.dir / "nested" / "blob.bin";

    const std::vector<uint8_t> bytes = {0x00, 0xFF, 0x10, 0x42, 0x7F, 0x80, 0xAB, 0xCD};
    REQUIRE(writeFile(file, bytes));

    const std::optional<std::vector<uint8_t>> read = readFile(file);
    REQUIRE(read.has_value());
    REQUIRE(*read == bytes);
}

TEST_CASE("writeFile/readFile handle an empty payload", "[userstorage]") {
    ScopedTestDir guard;
    const std::filesystem::path file = guard.dir / "empty.bin";

    REQUIRE(writeFile(file, std::span<const uint8_t>{}));

    const std::optional<std::vector<uint8_t>> read = readFile(file);
    REQUIRE(read.has_value());
    REQUIRE(read->empty());
}

TEST_CASE("writeTextFile/readTextFile round-trips text", "[userstorage]") {
    ScopedTestDir guard;
    const std::filesystem::path file = guard.dir / "note.txt";

    const std::string text = "line one\nline two\nDSSETTINGS-ish 1\n";
    REQUIRE(writeTextFile(file, text));

    const std::optional<std::string> read = readTextFile(file);
    REQUIRE(read.has_value());
    REQUIRE(*read == text);
}

TEST_CASE("saveSettings/loadSettings round-trips a GameSettings", "[userstorage]") {
    ScopedTestDir guard;

    GameSettings in;
    in.masterVolume    = 0.5f;
    in.sfxVolume       = 0.75f;
    in.musicVolume     = 0.25f;
    in.lookSensitivity = 0.2f;
    in.windowWidth     = 1920;
    in.windowHeight    = 1080;
    in.fullscreen      = true;
    in.vsync           = false;

    REQUIRE(saveSettings(guard.dir, in));

    const std::optional<GameSettings> out = loadSettings(guard.dir);
    REQUIRE(out.has_value());
    REQUIRE(out->masterVolume == Catch::Approx(in.masterVolume));
    REQUIRE(out->sfxVolume == Catch::Approx(in.sfxVolume));
    REQUIRE(out->musicVolume == Catch::Approx(in.musicVolume));
    REQUIRE(out->lookSensitivity == Catch::Approx(in.lookSensitivity));
    REQUIRE(out->windowWidth == in.windowWidth);
    REQUIRE(out->windowHeight == in.windowHeight);
    REQUIRE(out->fullscreen == in.fullscreen);
    REQUIRE(out->vsync == in.vsync);
}

TEST_CASE("saveGame/loadGame round-trips a SaveData", "[userstorage]") {
    ScopedTestDir guard;

    SaveData in{};
    in.bestWave    = 12;
    in.highScore   = 987654;
    in.totalKills  = 4242;
    in.totalRuns   = 37;
    in.unlockFlags = 0xDEADBEEFu;
    in.bestCombo   = 88;

    REQUIRE(saveGame(guard.dir, in));

    const std::optional<SaveData> out = loadGame(guard.dir);
    REQUIRE(out.has_value());
    REQUIRE(out->bestWave == in.bestWave);
    REQUIRE(out->highScore == in.highScore);
    REQUIRE(out->totalKills == in.totalKills);
    REQUIRE(out->totalRuns == in.totalRuns);
    REQUIRE(out->unlockFlags == in.unlockFlags);
    REQUIRE(out->bestCombo == in.bestCombo);
}

TEST_CASE("reading missing files yields nullopt", "[userstorage]") {
    ScopedTestDir guard; // dir does not exist yet

    REQUIRE_FALSE(readFile(guard.dir / "nope.bin").has_value());
    REQUIRE_FALSE(readTextFile(guard.dir / "nope.txt").has_value());
    REQUIRE_FALSE(loadSettings(guard.dir).has_value());
    REQUIRE_FALSE(loadGame(guard.dir).has_value());
}
