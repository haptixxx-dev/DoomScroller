#include "engine/TextureFormatDstex.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <span>
#include <vector>

using namespace ds;

// These tests exercise the on-disk DSTX container directly: serialize then parse
// must round-trip the header + payload, and parse must reject malformed inputs.
// Pure (vector/span/optional/cstring) — links engine_headers only.

namespace {

std::vector<uint8_t> makePayload(std::size_t n) {
    std::vector<uint8_t> p(n);
    for (std::size_t i = 0; i < n; ++i) {
        p[i] = static_cast<uint8_t>(i * 7u + 1u);
    }
    return p;
}

DstexHeader makeHeader(uint32_t w, uint32_t h, std::size_t payloadBytes) {
    DstexHeader hdr{};
    hdr.width     = w;
    hdr.height    = h;
    hdr.mipLevels = 1;
    hdr.format    = static_cast<uint32_t>(DstexFormat::RGBA8);
    hdr.dataSize  = static_cast<uint32_t>(payloadBytes);
    return hdr;
}

} // namespace

TEST_CASE("DstexHeader size matches the documented on-disk layout", "[dstex]") {
    REQUIRE(sizeof(DstexHeader) == 32);
}

TEST_CASE("Magic spells 'DSTX' little-endian", "[dstex]") {
    const auto* bytes = reinterpret_cast<const char*>(&kDstexMagic);
    REQUIRE(bytes[0] == 'D');
    REQUIRE(bytes[1] == 'S');
    REQUIRE(bytes[2] == 'T');
    REQUIRE(bytes[3] == 'X');
}

TEST_CASE("DstexHeader defaults carry correct magic and version", "[dstex]") {
    DstexHeader hdr{};
    REQUIRE(hdr.magic == kDstexMagic);
    REQUIRE(hdr.version == kDstexVersion);
    REQUIRE(hdr.mipLevels == 1);
    REQUIRE(hdr.format == static_cast<uint32_t>(DstexFormat::RGBA8));
    REQUIRE(hdr.dataSize == 0);
}

TEST_CASE("serializeDstex then parseDstex round-trips header + payload", "[dstex]") {
    const std::vector<uint8_t> payload = makePayload(2u * 2u * 4u);
    const DstexHeader header           = makeHeader(2, 2, payload.size());

    const std::vector<uint8_t> blob = serializeDstex(header, payload);
    REQUIRE(blob.size() == sizeof(DstexHeader) + payload.size());

    const std::optional<DstexFile> parsed = parseDstex(blob);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->header.magic == kDstexMagic);
    REQUIRE(parsed->header.version == kDstexVersion);
    REQUIRE(parsed->header.width == 2);
    REQUIRE(parsed->header.height == 2);
    REQUIRE(parsed->header.mipLevels == 1);
    REQUIRE(parsed->header.format == static_cast<uint32_t>(DstexFormat::RGBA8));
    REQUIRE(parsed->header.dataSize == static_cast<uint32_t>(payload.size()));
    REQUIRE(parsed->data == payload);
}

TEST_CASE("serializeDstex with empty payload round-trips", "[dstex]") {
    const DstexHeader header        = makeHeader(0, 0, 0);
    const std::vector<uint8_t> blob = serializeDstex(header, {});
    REQUIRE(blob.size() == sizeof(DstexHeader));

    const std::optional<DstexFile> parsed = parseDstex(blob);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->header.dataSize == 0);
    REQUIRE(parsed->data.empty());
}

TEST_CASE("parseDstex rejects bad magic", "[dstex]") {
    const std::vector<uint8_t> payload = makePayload(16);
    std::vector<uint8_t> blob          = serializeDstex(makeHeader(2, 2, payload.size()), payload);

    blob[0] ^= 0xFF; // corrupt the magic's first byte
    REQUIRE_FALSE(parseDstex(blob).has_value());
}

TEST_CASE("parseDstex rejects wrong version", "[dstex]") {
    const std::vector<uint8_t> payload = makePayload(16);
    DstexHeader header                 = makeHeader(2, 2, payload.size());
    header.version                     = kDstexVersion + 1;

    const std::vector<uint8_t> blob = serializeDstex(header, payload);
    REQUIRE_FALSE(parseDstex(blob).has_value());
}

TEST_CASE("parseDstex rejects truncated payload", "[dstex]") {
    const std::vector<uint8_t> payload = makePayload(16);
    std::vector<uint8_t> blob          = serializeDstex(makeHeader(2, 2, payload.size()), payload);

    blob.pop_back(); // header.dataSize now exceeds the actual trailing bytes
    REQUIRE_FALSE(parseDstex(blob).has_value());
}

TEST_CASE("parseDstex rejects a buffer smaller than the header", "[dstex]") {
    std::vector<uint8_t> tooSmall(sizeof(DstexHeader) - 1, 0u);
    REQUIRE_FALSE(parseDstex(tooSmall).has_value());
}

TEST_CASE("parseDstex rejects dataSize mismatch vs payload", "[dstex]") {
    const std::vector<uint8_t> payload = makePayload(16);

    SECTION("dataSize too large") {
        DstexHeader header              = makeHeader(2, 2, payload.size());
        header.dataSize                 = static_cast<uint32_t>(payload.size() + 4u);
        const std::vector<uint8_t> blob = serializeDstex(header, payload);
        REQUIRE_FALSE(parseDstex(blob).has_value());
    }

    SECTION("dataSize too small") {
        DstexHeader header              = makeHeader(2, 2, payload.size());
        header.dataSize                 = static_cast<uint32_t>(payload.size() - 4u);
        const std::vector<uint8_t> blob = serializeDstex(header, payload);
        REQUIRE_FALSE(parseDstex(blob).has_value());
    }
}
