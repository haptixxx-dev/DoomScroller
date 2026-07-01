// Unit tests for the pure CPU half of the golden-image harness (PLAN task 47):
// PPM (P6) read/write round-trip and the per-pixel-tolerance image diff. The
// GPU capture half (offscreen render -> debugDownloadTexture) runs on the
// hardware testbench; these tests guard the format + comparison logic that
// gates every future render regression.

#include "engine/GoldenImage.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ds;

namespace {

Image makeGradient(uint32_t w, uint32_t h) {
    Image img(w, h);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            std::size_t i  = img.index(x, y);
            img.rgb[i]     = static_cast<uint8_t>(x % 256);
            img.rgb[i + 1] = static_cast<uint8_t>(y % 256);
            img.rgb[i + 2] = static_cast<uint8_t>((x + y) % 256);
        }
    }
    return img;
}

} // namespace

TEST_CASE("Image ctor allocates tightly packed RGB", "[golden]") {
    Image img(4, 3);
    REQUIRE(img.width == 4);
    REQUIRE(img.height == 3);
    REQUIRE(img.rgb.size() == 4u * 3u * 3u);
    REQUIRE(img.pixelCount() == 12u);
    REQUIRE_FALSE(img.empty());
    REQUIRE(Image().empty());
}

TEST_CASE("writePpm produces the debugDownloadTexture P6 byte layout", "[golden]") {
    Image img(2, 1);
    img.rgb                    = {10, 20, 30, 40, 50, 60};
    std::vector<uint8_t> bytes = writePpm(img);

    // Header is exactly "P6\n2 1\n255\n" followed by the 6 raw RGB bytes.
    std::string header = "P6\n2 1\n255\n";
    REQUIRE(bytes.size() == header.size() + 6);
    std::string got(bytes.begin(), bytes.begin() + static_cast<long>(header.size()));
    REQUIRE(got == header);
    REQUIRE(bytes[header.size() + 0] == 10);
    REQUIRE(bytes[header.size() + 5] == 60);
}

TEST_CASE("readPpm round-trips writePpm exactly", "[golden]") {
    Image original             = makeGradient(37, 19); // non-power-of-two, odd dimensions
    std::vector<uint8_t> bytes = writePpm(original);

    auto parsed = readPpm(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->width == original.width);
    REQUIRE(parsed->height == original.height);
    REQUIRE(parsed->rgb == original.rgb);
}

TEST_CASE("readPpm tolerates comments and extra whitespace in the header", "[golden]") {
    std::string hdr = "P6\n# a comment line\n  2   2  \n255\n";
    std::vector<uint8_t> bytes(hdr.begin(), hdr.end());
    for (int i = 0; i < 2 * 2 * 3; ++i)
        bytes.push_back(static_cast<uint8_t>(i));

    auto parsed = readPpm(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->width == 2);
    REQUIRE(parsed->height == 2);
    REQUIRE(parsed->rgb.size() == 12u);
    REQUIRE(parsed->rgb[11] == 11);
}

TEST_CASE("readPpm rejects malformed input", "[golden]") {
    SECTION("wrong magic") {
        std::vector<uint8_t> b = {'P', '3', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 0, 0, 0};
        REQUIRE_FALSE(readPpm(b).has_value());
    }
    SECTION("non-255 maxval") {
        std::string s = "P6\n1 1\n65535\n";
        std::vector<uint8_t> b(s.begin(), s.end());
        b.insert(b.end(), {0, 0, 0});
        REQUIRE_FALSE(readPpm(b).has_value());
    }
    SECTION("truncated pixel data") {
        std::string s = "P6\n4 4\n255\n";
        std::vector<uint8_t> b(s.begin(), s.end());
        b.push_back(1); // way short of 4*4*3 bytes
        REQUIRE_FALSE(readPpm(b).has_value());
    }
    SECTION("oversized dimensions do not allocate / crash") {
        // A corrupt header declaring huge dimensions must be rejected via the
        // 64-bit size check BEFORE any multi-gigabyte allocation is attempted,
        // returning nullopt rather than throwing std::bad_alloc.
        std::string s = "P6\n40000 40000\n255\n";
        std::vector<uint8_t> b(s.begin(), s.end());
        b.insert(b.end(), {0, 0, 0}); // only 3 raster bytes vs the ~4.8 GB claimed
        REQUIRE_FALSE(readPpm(b).has_value());
    }
    SECTION("empty buffer") {
        REQUIRE_FALSE(readPpm({}).has_value());
    }
}

TEST_CASE("writePpm/readPpm handle a zero-size image", "[golden]") {
    Image empty(0, 0);
    auto parsed = readPpm(writePpm(empty));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->width == 0);
    REQUIRE(parsed->height == 0);
    REQUIRE(parsed->rgb.empty());
}

TEST_CASE("compareImages matches identical images", "[golden]") {
    Image a     = makeGradient(16, 16);
    Image b     = a;
    ImageDiff d = compareImages(a, b);
    REQUIRE(d.sameSize);
    REQUIRE(d.match());
    REQUIRE(d.diffPixels == 0);
    REQUIRE(d.maxChannelDelta == 0);
    REQUIRE(d.meanChannelDelta == 0.0);
}

TEST_CASE("compareImages reports mismatched dimensions", "[golden]") {
    ImageDiff d = compareImages(Image(4, 4), Image(4, 5));
    REQUIRE_FALSE(d.sameSize);
    REQUIRE_FALSE(d.match());
    REQUIRE(d.diffFraction(16) == 1.0);
}

TEST_CASE("compareImages honours the per-channel tolerance", "[golden]") {
    Image a(2, 1);
    a.rgb    = {100, 100, 100, 100, 100, 100};
    Image b  = a;
    b.rgb[0] = 103; // +3 on one channel of pixel 0

    SECTION("delta exceeds a tolerance of 2") {
        ImageDiff d = compareImages(a, b, 2);
        REQUIRE(d.sameSize);
        REQUIRE_FALSE(d.match());
        REQUIRE(d.diffPixels == 1);
        REQUIRE(d.maxChannelDelta == 3);
    }
    SECTION("delta absorbed by a tolerance of 3") {
        ImageDiff d = compareImages(a, b, 3);
        REQUIRE(d.match());
        REQUIRE(d.diffPixels == 0);
        REQUIRE(d.maxChannelDelta == 3); // still reported, just under tolerance
    }
}

TEST_CASE("compareImages accumulates diff statistics", "[golden]") {
    Image a(2, 1);
    a.rgb = {0, 0, 0, 0, 0, 0};
    Image b(2, 1);
    b.rgb = {10, 0, 0, 0, 4, 0}; // pixel0 max delta 10, pixel1 max delta 4

    ImageDiff d = compareImages(a, b, 0);
    REQUIRE(d.diffPixels == 2);
    REQUIRE(d.maxChannelDelta == 10);
    // mean over 6 channels: (10+0+0+0+4+0)/6
    REQUIRE(d.meanChannelDelta == Catch::Approx(14.0 / 6.0));
    // Both pixels exceed a tolerance of 0, so the whole image differs.
    REQUIRE(d.diffFraction(2) == 1.0);
}

TEST_CASE("diffFraction reports a partial fraction", "[golden]") {
    Image a(2, 1);
    a.rgb = {0, 0, 0, 5, 5, 5};
    Image b(2, 1);
    b.rgb = {0, 0, 0, 20, 5, 5}; // only pixel 1 differs beyond tolerance

    ImageDiff d = compareImages(a, b, 3);
    REQUIRE(d.diffPixels == 1);
    REQUIRE(d.diffFraction(2) == 0.5);
}
