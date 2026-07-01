#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

// =============================================================================
// DoomScroller save-data blob ("DSSV")
// =============================================================================
//
// A compact, versioned, little-endian binary blob holding persistent player
// progression (best wave, high score, lifetime kill/run totals, unlock flags,
// best combo). It is the on-disk save core; CI/packaging concerns live
// elsewhere. The blob is self-describing and integrity-checked:
//
//   [ magic   u32 ]  must equal kSaveMagic
//   [ version u32 ]  must equal kSaveVersion for this reader
//   [ crc     u32 ]  CRC32 (IEEE 0xEDB88320) of the payload bytes only
//   [ payload     ]  SaveData fields, in declared order, fixed size
//
// All multi-byte fields are written little-endian via memcpy of the in-memory
// representation, matching LevelFormat.h: portable between little-endian hosts
// (x86-64, ARM little-endian), which covers every platform DoomScroller targets.
//
// VERSIONING
// ----------
// `version` is bumped whenever the payload layout changes. A reader must reject
// blobs whose version it does not understand rather than misinterpret bytes.
//
// INTEGRITY
// ---------
// The CRC32 covers exactly the payload bytes, so any flipped payload byte (or a
// payload truncated mid-field) is detected and rejected by parseSave.
// =============================================================================

namespace ds {

// Four-character magic 'DSSV' stored as a little-endian uint32. Spelled out as
// bytes so the value is endianness-explicit regardless of how it is compared.
inline constexpr uint32_t kSaveMagic = ('D') | ('S' << 8) | ('S' << 16) | (static_cast<uint32_t>('V') << 24);

// Current blob version. Bump on any payload layout change.
//   v1: bestWave, highScore, totalKills, totalRuns, unlockFlags, bestCombo.
//   v2: appended currency, upgradeRanks0, upgradeRanks1, difficulty (Phase 4
//       Wave D meta-progression / weapon economy / difficulty). Fields are
//       appended only — declared order is a memcpy contract — so a v1 blob is
//       simply rejected by parseSave (version gate) and resets to defaults.
inline constexpr uint32_t kSaveVersion = 2;

// Standard IEEE CRC32 (reflected, polynomial 0xEDB88320). Computes the checksum
// of `len` bytes at `data`. The 256-entry lookup table is built once at compile
// time so this is a pure header-only constexpr function with no global state.
constexpr uint32_t crc32(const uint8_t* data, size_t len) {
    constexpr uint32_t kPoly = 0xEDB88320u;

    // Build the byte-wise CRC table at compile time.
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (kPoly ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// Persistent player progression. This is the blob payload: eleven fixed-width
// fields written in declared order. 44 bytes on disk.
//
// LAYOUT IS APPEND-ONLY. The serializer memcpy's the whole struct in declared
// order, so the first six fields keep their v1 offsets and new fields are only
// ever appended after bestCombo. Reordering or inserting would silently
// remap on-disk bytes; bump kSaveVersion instead.
//
// NOTE: unlockFlags and econUnlockMask are DISJOINT bitsets in separate fields.
// unlockFlags holds MetaProgression::Unlock bits (wave-threshold auto-unlocks);
// econUnlockMask holds WeaponEconomy EconNode unlock bits (purchased weapon
// slots). Keeping them in separate words avoids the low-bit collision that a
// single shared field would create (both enums start at bit 0).
struct SaveData {
    uint32_t bestWave    = 0; // furthest wave reached
    uint32_t highScore   = 0; // best single-run score
    uint32_t totalKills  = 0; // lifetime enemy kills
    uint32_t totalRuns   = 0; // lifetime runs started
    uint32_t unlockFlags = 0; // MetaProgression::Unlock bitset ONLY (not shared with the economy)
    uint32_t bestCombo   = 0; // best combo multiplier reached
    // --- v2 (appended) --------------------------------------------------------
    uint32_t currency       = 0; // spendable meta-currency (WeaponEconomy)
    uint32_t upgradeRanks0  = 0; // EconNode ranks 0..3, one packed uint8 each
    uint32_t upgradeRanks1  = 0; // EconNode ranks 4..7, one packed uint8 each
    uint32_t econUnlockMask = 0; // WeaponEconomy EconNode unlock bitset (disjoint from unlockFlags)
    uint32_t difficulty     = 0; // selected difficulty index (0-based; wave.lua is 1-based)
};

static_assert(sizeof(SaveData) == 44, "SaveData payload must be 44 bytes on disk");

namespace detail {

// Number of payload bytes (the SaveData fields, contiguous).
inline constexpr size_t kSavePayloadSize = sizeof(SaveData);
// Header: magic + version + crc, each a uint32.
inline constexpr size_t kSaveHeaderSize = 3 * sizeof(uint32_t);
// Total well-formed blob size.
inline constexpr size_t kSaveBlobSize = kSaveHeaderSize + kSavePayloadSize;

// Append `value` to `out` as 4 little-endian bytes.
inline void putU32(std::vector<uint8_t>& out, uint32_t value) {
    uint8_t bytes[sizeof(uint32_t)];
    std::memcpy(bytes, &value, sizeof(uint32_t));
    out.insert(out.end(), bytes, bytes + sizeof(uint32_t));
}

// Read a little-endian uint32 from `src` at byte offset `offset`.
inline uint32_t getU32(std::span<const uint8_t> src, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, src.data() + offset, sizeof(uint32_t));
    return value;
}

} // namespace detail

// Serialize `save` into a self-describing blob:
// [magic][version][crc-of-payload][payload]. The CRC covers only the payload.
inline std::vector<uint8_t> serializeSave(const SaveData& save) {
    // Lay out the payload bytes first so we can checksum them.
    std::array<uint8_t, detail::kSavePayloadSize> payload{};
    std::memcpy(payload.data(), &save, detail::kSavePayloadSize);

    const uint32_t crc = crc32(payload.data(), payload.size());

    std::vector<uint8_t> out;
    out.reserve(detail::kSaveBlobSize);
    detail::putU32(out, kSaveMagic);
    detail::putU32(out, kSaveVersion);
    detail::putU32(out, crc);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// Parse a blob produced by serializeSave. Returns the decoded SaveData, or
// std::nullopt if the blob is too short, has a bad magic, an unsupported
// version, or a payload that fails its CRC check.
inline std::optional<SaveData> parseSave(std::span<const uint8_t> blob) {
    if (blob.size() < detail::kSaveBlobSize) {
        return std::nullopt;
    }

    const uint32_t magic   = detail::getU32(blob, 0);
    const uint32_t version = detail::getU32(blob, sizeof(uint32_t));
    const uint32_t crc     = detail::getU32(blob, 2 * sizeof(uint32_t));

    if (magic != kSaveMagic) {
        return std::nullopt;
    }
    if (version != kSaveVersion) {
        return std::nullopt;
    }

    const uint8_t* payload = blob.data() + detail::kSaveHeaderSize;
    if (crc32(payload, detail::kSavePayloadSize) != crc) {
        return std::nullopt;
    }

    SaveData save{};
    std::memcpy(&save, payload, detail::kSavePayloadSize);
    return save;
}

} // namespace ds
