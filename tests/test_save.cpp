#include "engine/MetaProgression.h"
#include "engine/SaveData.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

using namespace ds;

TEST_CASE("serializeSave round-trips through parseSave", "[save]") {
    SaveData in{};
    in.bestWave      = 12;
    in.highScore     = 987654;
    in.totalKills    = 4242;
    in.totalRuns     = 37;
    in.unlockFlags   = 0xDEADBEEFu;
    in.bestCombo     = 88;
    in.currency      = 500;
    in.upgradeRanks0 = 0x01020304u;
    in.upgradeRanks1 = 0x05060708u;
    in.difficulty    = 2;

    const std::vector<uint8_t> blob   = serializeSave(in);
    const std::optional<SaveData> out = parseSave(blob);

    REQUIRE(out.has_value());
    REQUIRE(out->bestWave == in.bestWave);
    REQUIRE(out->highScore == in.highScore);
    REQUIRE(out->totalKills == in.totalKills);
    REQUIRE(out->totalRuns == in.totalRuns);
    REQUIRE(out->unlockFlags == in.unlockFlags);
    REQUIRE(out->bestCombo == in.bestCombo);
    REQUIRE(out->currency == in.currency);
    REQUIRE(out->upgradeRanks0 == in.upgradeRanks0);
    REQUIRE(out->upgradeRanks1 == in.upgradeRanks1);
    REQUIRE(out->difficulty == in.difficulty);
}

TEST_CASE("SaveData v2 is 40 bytes and version 2", "[save]") {
    REQUIRE(sizeof(SaveData) == 40);
    REQUIRE(kSaveVersion == 2u);
    REQUIRE(detail::kSavePayloadSize == sizeof(SaveData));
    REQUIRE(serializeSave(SaveData{}).size() == detail::kSaveBlobSize);
}

TEST_CASE("named Unlock bits survive a serialize/parse round-trip", "[save]") {
    SaveData in{};
    setUnlock(in, Unlock::AltFire);
    setUnlock(in, Unlock::HardMode);

    const std::optional<SaveData> out = parseSave(serializeSave(in));
    REQUIRE(out.has_value());
    REQUIRE(isUnlocked(*out, Unlock::AltFire));
    REQUIRE(isUnlocked(*out, Unlock::HardMode));
    REQUIRE_FALSE(isUnlocked(*out, Unlock::ExtraWeapon));
}

TEST_CASE("a v1-versioned blob is rejected under v2", "[save]") {
    // Forge a well-formed blob whose version field says 1: magic, version=1,
    // then a correct CRC over a 40-byte v2 payload. The version gate must reject
    // it before the payload is trusted (old v1 saves reset to defaults).
    std::vector<uint8_t> blob = serializeSave(SaveData{});
    const uint32_t v1         = 1u;
    std::memcpy(blob.data() + sizeof(uint32_t), &v1, sizeof(uint32_t));
    // Recompute the CRC so it is NOT a CRC failure we are detecting, but the
    // version mismatch specifically.
    const uint8_t* payload = blob.data() + 3 * sizeof(uint32_t);
    const uint32_t crc     = crc32(payload, blob.size() - 3 * sizeof(uint32_t));
    std::memcpy(blob.data() + 2 * sizeof(uint32_t), &crc, sizeof(uint32_t));

    REQUIRE_FALSE(parseSave(blob).has_value());
}

TEST_CASE("default SaveData round-trips", "[save]") {
    const SaveData in{};
    const std::optional<SaveData> out = parseSave(serializeSave(in));

    REQUIRE(out.has_value());
    REQUIRE(out->bestWave == 0);
    REQUIRE(out->highScore == 0);
    REQUIRE(out->totalKills == 0);
    REQUIRE(out->totalRuns == 0);
    REQUIRE(out->unlockFlags == 0);
    REQUIRE(out->bestCombo == 0);
}

TEST_CASE("flipping a payload byte fails the CRC check", "[save]") {
    SaveData in{};
    in.highScore = 1000;

    std::vector<uint8_t> blob = serializeSave(in);
    REQUIRE(parseSave(blob).has_value());

    // The payload begins after the 12-byte header (magic + version + crc).
    constexpr size_t kHeaderBytes = 3 * sizeof(uint32_t);
    blob[kHeaderBytes] ^= 0x01u; // corrupt the first payload byte

    REQUIRE_FALSE(parseSave(blob).has_value());
}

TEST_CASE("wrong magic is rejected", "[save]") {
    std::vector<uint8_t> blob = serializeSave(SaveData{});

    const uint32_t badMagic = kSaveMagic ^ 0xFFu;
    std::memcpy(blob.data(), &badMagic, sizeof(uint32_t));

    REQUIRE_FALSE(parseSave(blob).has_value());
}

TEST_CASE("wrong version is rejected", "[save]") {
    std::vector<uint8_t> blob = serializeSave(SaveData{});

    const uint32_t badVersion = kSaveVersion + 1;
    std::memcpy(blob.data() + sizeof(uint32_t), &badVersion, sizeof(uint32_t));

    REQUIRE_FALSE(parseSave(blob).has_value());
}

TEST_CASE("truncated blob is rejected", "[save]") {
    const std::vector<uint8_t> blob = serializeSave(SaveData{});
    REQUIRE(blob.size() > 1);

    // Drop the last byte so the blob is shorter than a well-formed one.
    const std::span<const uint8_t> truncated(blob.data(), blob.size() - 1);
    REQUIRE_FALSE(parseSave(truncated).has_value());

    // An empty blob is also rejected.
    REQUIRE_FALSE(parseSave(std::span<const uint8_t>{}).has_value());
}
