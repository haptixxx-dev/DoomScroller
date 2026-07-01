// Unit tests for the pure texture-readback -> RGB8 conversion (Phase 4 task 48
// pre-work). This is the CPU half of SDL3Device::debugDownloadTexture, factored
// out so the golden-image capture path is verifiable without a GPU — and so the
// original "always 4 bytes/px RGBA8 swizzled by the swapchain format" bug (which
// could not read back the RGBA16Float HDR target) is caught here.

#include "engine/rhi/TextureReadback.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace ds::rhi;

namespace {

// Encode a float as an IEEE-754 half (binary16) for the RGBA16Float cases.
uint16_t floatToHalf(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exp         = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    const uint32_t mant = bits & 0x7FFFFFu;
    if (exp <= 0) {
        return static_cast<uint16_t>(sign); // flush tiny values to signed zero (sufficient for tests)
    }
    if (exp >= 0x1F) {
        return static_cast<uint16_t>(sign | 0x7C00u); // inf
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
}

} // namespace

TEST_CASE("bytesPerPixel matches each format's layout", "[readback]") {
    REQUIRE(bytesPerPixel(TextureFormat::R8Unorm) == 1u);
    REQUIRE(bytesPerPixel(TextureFormat::RG8Unorm) == 2u);
    REQUIRE(bytesPerPixel(TextureFormat::RGBA8Unorm) == 4u);
    REQUIRE(bytesPerPixel(TextureFormat::BGRA8Unorm) == 4u);
    REQUIRE(bytesPerPixel(TextureFormat::R32Float) == 4u);
    REQUIRE(bytesPerPixel(TextureFormat::RGBA16Float) == 8u);
    REQUIRE(bytesPerPixel(TextureFormat::BC7Unorm) == 0u); // block-compressed, not a readback source
}

TEST_CASE("RGBA8 passes RGB through and drops alpha", "[readback]") {
    std::vector<uint8_t> src = {10, 20, 30, 255, 40, 50, 60, 0};
    auto rgb                 = convertToRgb8(src.data(), 2, TextureFormat::RGBA8Unorm);
    REQUIRE(rgb.size() == 6u);
    REQUIRE(rgb[0] == 10);
    REQUIRE(rgb[1] == 20);
    REQUIRE(rgb[2] == 30);
    REQUIRE(rgb[3] == 40);
    REQUIRE(rgb[5] == 60);
}

TEST_CASE("BGRA8 swaps B and R", "[readback]") {
    // One pixel, stored B,G,R,A = 30,20,10,255 -> RGB should be 10,20,30.
    std::vector<uint8_t> src = {30, 20, 10, 255};
    auto rgb                 = convertToRgb8(src.data(), 1, TextureFormat::BGRA8Unorm);
    REQUIRE(rgb.size() == 3u);
    REQUIRE(rgb[0] == 10);
    REQUIRE(rgb[1] == 20);
    REQUIRE(rgb[2] == 30);
}

TEST_CASE("R8 expands to grayscale, RG8 fills blue with zero", "[readback]") {
    std::vector<uint8_t> r8 = {77};
    auto grey               = convertToRgb8(r8.data(), 1, TextureFormat::R8Unorm);
    REQUIRE(grey[0] == 77);
    REQUIRE(grey[1] == 77);
    REQUIRE(grey[2] == 77);

    std::vector<uint8_t> rg8 = {11, 22};
    auto rg                  = convertToRgb8(rg8.data(), 1, TextureFormat::RG8Unorm);
    REQUIRE(rg[0] == 11);
    REQUIRE(rg[1] == 22);
    REQUIRE(rg[2] == 0);
}

TEST_CASE("RGBA16Float decodes halves and clamps to [0,1]", "[readback]") {
    // Three halves: 0.0, 0.5, 2.0 (over-range, must clamp to 255). Plus an
    // alpha half that is ignored.
    uint16_t halves[4] = {floatToHalf(0.0f), floatToHalf(0.5f), floatToHalf(2.0f), floatToHalf(1.0f)};
    std::vector<uint8_t> src(8);
    std::memcpy(src.data(), halves, 8);

    auto rgb = convertToRgb8(src.data(), 1, TextureFormat::RGBA16Float);
    REQUIRE(rgb.size() == 3u);
    REQUIRE(rgb[0] == 0);   // 0.0
    REQUIRE(rgb[1] == 128); // 0.5 -> 0.5*255+0.5 = 128
    REQUIRE(rgb[2] == 255); // 2.0 clamps
}

TEST_CASE("R32Float decodes and clamps to grayscale", "[readback]") {
    float v = 0.25f;
    std::vector<uint8_t> src(4);
    std::memcpy(src.data(), &v, 4);
    auto rgb = convertToRgb8(src.data(), 1, TextureFormat::R32Float);
    REQUIRE(rgb[0] == 64); // 0.25*255+0.5 = 64
    REQUIRE(rgb[0] == rgb[1]);
    REQUIRE(rgb[1] == rgb[2]);
}

TEST_CASE("depth / BC7 / empty inputs yield an empty buffer", "[readback]") {
    std::vector<uint8_t> src(64, 0);
    REQUIRE(convertToRgb8(src.data(), 4, TextureFormat::D32Float).empty());
    REQUIRE(convertToRgb8(src.data(), 4, TextureFormat::BC7Unorm).empty());
    REQUIRE(convertToRgb8(nullptr, 4, TextureFormat::RGBA8Unorm).empty());
    REQUIRE(convertToRgb8(src.data(), 0, TextureFormat::RGBA8Unorm).empty());
}
