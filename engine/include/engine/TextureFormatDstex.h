#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

// =============================================================================
// DoomScroller cooked-texture format ("DSTX" / .dstex)
// =============================================================================
//
// A compact, versioned, little-endian binary container for a single GPU texture
// produced offline by the asset cook tool. Like the level format (LevelFormat.h)
// it stores fixed-width integers in native (little-endian) order; no byte
// swapping is performed on read, which is portable across every platform
// DoomScroller targets (x86-64, ARM little-endian).
//
// FILE LAYOUT
// -----------
//   [ DstexHeader                 ]  (32 bytes)
//   [ payload : header.dataSize   ]  (raw pixel / block bytes)
//
// The payload is the texture's level-0 (and, when mipLevels > 1, subsequent
// mip) bytes laid out tightly for the declared `format`. For RGBA8 that is
// width*height*4 bytes for the base level; for BC7 it is the packed 4x4 block
// stream. `dataSize` is authoritative: a reader allocates and copies exactly
// that many bytes and rejects any file whose trailing bytes do not match.
//
// VERSIONING
// ----------
// `version` is bumped whenever the on-disk layout changes. A reader must reject
// a file whose version it does not understand rather than misinterpret bytes.
//
// FORMATS
// -------
// `format` selects the payload encoding:
//   0 = RGBA8  : 8-bit-per-channel unsigned normalized, 4 bytes/texel.
//   1 = BC7    : BC7 block-compressed (16 bytes per 4x4 block).
// The cook tool currently emits only RGBA8 (format 0); the BC7 path is a
// documented seam (see asset_cook.cpp) awaiting a real encoder extern.
// =============================================================================

namespace ds {

// Four-character magic 'DSTX' stored as a little-endian uint32. Spelled out as
// bytes so the value is endianness-explicit regardless of how it is compared.
inline constexpr uint32_t kDstexMagic = ('D') | ('S' << 8) | ('T' << 16) | (static_cast<uint32_t>('X') << 24);

// Current format version. Bump on any layout change.
inline constexpr uint32_t kDstexVersion = 1;

// Payload encodings for DstexHeader::format.
enum class DstexFormat : uint32_t {
    RGBA8 = 0, // 4 bytes per texel, uncompressed
    BC7   = 1, // BC7 block-compressed, 16 bytes per 4x4 block
};

// File header. 32 bytes. `dataSize` is the exact length of the payload that
// follows; the two reserved words are zero and exist to keep the header
// 8-field/32-byte aligned for future fields without a version bump churn.
struct DstexHeader {
    uint32_t magic     = kDstexMagic;                               // must equal kDstexMagic
    uint32_t version   = kDstexVersion;                             // must equal kDstexVersion
    uint32_t width     = 0;                                         // texels, base mip
    uint32_t height    = 0;                                         // texels, base mip
    uint32_t mipLevels = 1;                                         // number of mips in payload (>= 1)
    uint32_t format    = static_cast<uint32_t>(DstexFormat::RGBA8); // DstexFormat value
    uint32_t dataSize  = 0;                                         // payload byte count
    uint32_t reserved0 = 0;                                         // reserved, must be 0
};

static_assert(sizeof(DstexHeader) == 32, "DstexHeader must be 32 bytes on disk");

// A parsed .dstex: the validated header plus an owning copy of the payload.
struct DstexFile {
    DstexHeader header;
    std::vector<uint8_t> data;
};

// Serialize a header + payload into a single contiguous buffer: [header][payload].
// The returned buffer's header.dataSize is taken from the supplied `header`; the
// caller is responsible for ensuring header.dataSize == payload.size() (the cook
// tool sets it from the payload, and parseDstex enforces the match on read).
inline std::vector<uint8_t> serializeDstex(const DstexHeader& header, std::span<const uint8_t> payload) {
    std::vector<uint8_t> out;
    out.resize(sizeof(DstexHeader) + payload.size());
    std::memcpy(out.data(), &header, sizeof(DstexHeader));
    if (!payload.empty()) {
        std::memcpy(out.data() + sizeof(DstexHeader), payload.data(), payload.size());
    }
    return out;
}

// Parse + validate a .dstex byte blob. Returns std::nullopt when the bytes are
// not a well-formed file this reader understands:
//   - too small to contain a header,
//   - magic != kDstexMagic,
//   - version != kDstexVersion,
//   - declared dataSize does not exactly match the trailing byte count
//     (truncated or over-long).
inline std::optional<DstexFile> parseDstex(std::span<const uint8_t> bytes) {
    if (bytes.size() < sizeof(DstexHeader)) {
        return std::nullopt;
    }

    DstexHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(DstexHeader));

    if (header.magic != kDstexMagic) {
        return std::nullopt;
    }
    if (header.version != kDstexVersion) {
        return std::nullopt;
    }

    const std::size_t payloadBytes = bytes.size() - sizeof(DstexHeader);
    if (header.dataSize != payloadBytes) {
        return std::nullopt;
    }

    DstexFile file;
    file.header = header;
    file.data.resize(payloadBytes);
    if (payloadBytes > 0) {
        std::memcpy(file.data.data(), bytes.data() + sizeof(DstexHeader), payloadBytes);
    }
    return file;
}

} // namespace ds
