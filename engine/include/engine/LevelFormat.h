#pragma once

#include <cstdint>

// =============================================================================
// DoomScroller binary level format ("DSLV")
// =============================================================================
//
// A compact, versioned, little-endian binary container for static level data.
// All multi-byte fields are stored little-endian; no byte swapping is performed
// on read, so this format is portable only between little-endian hosts (x86-64,
// ARM little-endian) which covers every platform DoomScroller targets.
//
// Fixed-width integer types are used throughout so the on-disk layout is stable
// regardless of the compiler's `int`/`long` widths. All structs are 4-byte
// packed naturally (every member is 4 bytes or a multiple), so no explicit
// padding directives are required and sizeof() matches the byte count on disk.
//
// FILE LAYOUT
// -----------
//   [ LevelHeader                                  ]  (32 bytes)
//   [ BoxRecord        * header.boxCount           ]  (40 bytes each)
//   [ SpawnPointRecord * header.spawnCount         ]  (16 bytes each)
//   [ LightRecord      * header.lightCount         ]  (32 bytes each)
//
// Records appear contiguously in the order above; counts in the header tell the
// reader how many of each follow. A reader should validate magic + version, then
// read exactly the advertised number of each record. Trailing bytes are ignored.
//
// VERSIONING
// ----------
// `version` is bumped whenever the on-disk layout of any record changes. A
// reader must reject files whose version it does not understand rather than
// silently misinterpret bytes.
// =============================================================================

namespace ds {

// Four-character magic 'DSLV' stored as a little-endian uint32. Spelled out as
// bytes so the value is endianness-explicit regardless of how it is compared.
inline constexpr uint32_t kLevelMagic = ('D') | ('S' << 8) | ('L' << 16) | (static_cast<uint32_t>('V') << 24);

// Current format version. Bump on any record layout change.
inline constexpr uint32_t kLevelVersion = 1;

// File header. 32 bytes. Counts describe how many of each record block follow.
struct LevelHeader {
    uint32_t magic      = kLevelMagic;   // must equal kLevelMagic
    uint32_t version    = kLevelVersion; // must equal kLevelVersion for this reader
    uint32_t boxCount   = 0;             // number of BoxRecord entries
    uint32_t spawnCount = 0;             // number of SpawnPointRecord entries
    uint32_t lightCount = 0;             // number of LightRecord entries
    uint32_t reserved0  = 0;             // reserved for future use, must be 0
    uint32_t reserved1  = 0;             // reserved for future use, must be 0
    uint32_t reserved2  = 0;             // reserved for future use, must be 0
};

// Static box geometry: a collision + render box. `materialRef` is an index into
// a future material table (task 15); 0 means the default checkerboard material.
struct BoxRecord {
    float center[3]      = {0.f, 0.f, 0.f}; // world-space center
    float halfExtents[3] = {1.f, 1.f, 1.f}; // half-size along each axis
    float color[3]       = {1.f, 1.f, 1.f}; // vertex tint (rgb 0..1)
    uint32_t materialRef = 0;               // material table index, 0 = default
};

// Player / enemy spawn location.
struct SpawnPointRecord {
    float position[3] = {0.f, 0.f, 0.f};
    uint32_t flags    = 0; // bit0: player start; reserved otherwise
};

// Light definition. Reserved ahead of the lighting task (task 15); the loader
// reads these but does not yet create light entities.
struct LightRecord {
    float position[3] = {0.f, 0.f, 0.f};
    float color[3]    = {1.f, 1.f, 1.f}; // rgb 0..1
    float radius      = 10.f;            // falloff radius in world units
    float intensity   = 1.f;             // scalar brightness
};

static_assert(sizeof(LevelHeader) == 32, "LevelHeader must be 32 bytes on disk");
static_assert(sizeof(BoxRecord) == 40, "BoxRecord must be 40 bytes on disk");
static_assert(sizeof(SpawnPointRecord) == 16, "SpawnPointRecord must be 16 bytes on disk");
static_assert(sizeof(LightRecord) == 32, "LightRecord must be 32 bytes on disk");

} // namespace ds
