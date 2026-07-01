#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// Golden-image (reference-image) support for the render-regression harness
// (PLAN task 47). This header is the pure, CPU-only half: an 8-bit RGB image
// container, binary PPM (P6) read/write, and a per-pixel-tolerance diff. It
// deliberately links nothing (std-only) so it compiles into the headless
// engine_math/engine_headers test target and is unit-testable in a sandbox
// with no GPU.
//
// The P6 byte layout produced by writePpm() is intentionally identical to the
// one SDL3Device::debugDownloadTexture writes on real hardware ("P6\n<w> <h>\n
// 255\n" followed by width*height*3 tightly packed RGB bytes), so a reference
// image captured on the GPU testbench round-trips through readPpm() here and
// diffs against a freshly captured frame with no format translation.
namespace ds {

// Minimal 8-bit-per-channel RGB image. Row-major, tightly packed, top-to-
// bottom (matching the debugDownloadTexture readback order).
struct Image {
    uint32_t width  = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgb; // size == width * height * 3

    Image() = default;
    Image(uint32_t w, uint32_t h) : width(w), height(h), rgb(static_cast<std::size_t>(w) * h * 3, 0) {}

    std::size_t pixelCount() const { return static_cast<std::size_t>(width) * height; }
    bool empty() const { return width == 0 || height == 0; }

    // Byte offset of pixel (x, y)'s red channel. No bounds checking.
    std::size_t index(uint32_t x, uint32_t y) const { return (static_cast<std::size_t>(y) * width + x) * 3; }
};

// Result of comparing two images with compareImages().
struct ImageDiff {
    bool sameSize           = false; // false => dimensions differed, other fields are 0
    uint64_t diffPixels     = 0;     // pixels whose max channel delta exceeded the tolerance
    int maxChannelDelta     = 0;     // largest single-channel absolute difference seen
    double meanChannelDelta = 0.0;   // mean absolute per-channel difference over all channels

    // A run "passes" when the images are the same size and no pixel exceeds the
    // tolerance. Kept as a method so callers read match() rather than poking
    // fields.
    bool match() const { return sameSize && diffPixels == 0; }

    // Fraction of pixels that exceeded tolerance (0 when sizes differ).
    double diffFraction(std::size_t totalPixels) const {
        if (!sameSize || totalPixels == 0)
            return sameSize ? 0.0 : 1.0;
        return static_cast<double>(diffPixels) / static_cast<double>(totalPixels);
    }
};

namespace detail {

// Skip whitespace + '#' comment lines in a PPM header, per the netpbm spec.
inline void skipPpmWhitespace(const std::string& s, std::size_t& pos) {
    while (pos < s.size()) {
        char c = s[pos];
        if (c == '#') {
            // Comment runs to end of line.
            while (pos < s.size() && s[pos] != '\n')
                ++pos;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++pos;
        } else {
            break;
        }
    }
}

// Parse a base-10 unsigned integer token. Returns false if no digits present.
inline bool parsePpmUint(const std::string& s, std::size_t& pos, uint32_t& out) {
    skipPpmWhitespace(s, pos);
    if (pos >= s.size() || s[pos] < '0' || s[pos] > '9')
        return false;
    uint64_t v = 0;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
        v = v * 10 + static_cast<uint64_t>(s[pos] - '0');
        if (v > 0xFFFFFFFFull) // overflow guard
            return false;
        ++pos;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

} // namespace detail

// Serialize an image to a binary PPM (P6) byte buffer, byte-identical to the
// on-GPU debugDownloadTexture output. Empty images yield a valid 0x0 header.
inline std::vector<uint8_t> writePpm(const Image& img) {
    std::string header = "P6\n" + std::to_string(img.width) + " " + std::to_string(img.height) + "\n255\n";
    std::vector<uint8_t> out;
    out.reserve(header.size() + img.rgb.size());
    out.insert(out.end(), header.begin(), header.end());
    out.insert(out.end(), img.rgb.begin(), img.rgb.end());
    return out;
}

// Parse a binary PPM (P6). Returns std::nullopt if the magic number is not
// "P6", the header is malformed, the max-value is not 255, or the pixel data
// is shorter than width*height*3. Tolerates comment lines and arbitrary
// whitespace in the header, and ignores trailing bytes past the pixel block.
inline std::optional<Image> readPpm(const std::vector<uint8_t>& bytes) {
    std::string s(bytes.begin(), bytes.end());
    std::size_t pos = 0;
    if (s.size() < 2 || s[0] != 'P' || s[1] != '6')
        return std::nullopt;
    pos = 2;

    uint32_t w = 0, h = 0, maxval = 0;
    if (!detail::parsePpmUint(s, pos, w))
        return std::nullopt;
    if (!detail::parsePpmUint(s, pos, h))
        return std::nullopt;
    if (!detail::parsePpmUint(s, pos, maxval))
        return std::nullopt;
    if (maxval != 255)
        return std::nullopt;

    // Per the P6 spec exactly one whitespace byte separates the header from the
    // binary raster. Consume it (any single whitespace char).
    if (pos >= s.size())
        return std::nullopt;
    char sep = s[pos];
    if (sep != ' ' && sep != '\t' && sep != '\r' && sep != '\n')
        return std::nullopt;
    ++pos;

    // Validate the declared raster size against the bytes actually present
    // BEFORE allocating. A corrupt header (e.g. "40000 40000") would otherwise
    // request gigabytes and throw an uncaught std::bad_alloc; and on a 32-bit
    // size_t the width*height*3 product in Image's ctor could wrap, breaking
    // the rgb.size() == w*h*3 invariant. Computing `need` in 64-bit and
    // rejecting a short/oversized buffer keeps the contract (nullopt, never a
    // crash) and preserves the struct invariant.
    const uint64_t need = static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * 3ull;
    if (need > static_cast<uint64_t>(bytes.size() - pos))
        return std::nullopt;

    Image img(w, h);
    for (std::size_t i = 0; i < static_cast<std::size_t>(need); ++i)
        img.rgb[i] = bytes[pos + i];
    return img;
}

// Compare two images channel-by-channel. `tolerance` is the maximum absolute
// per-channel difference (0..255) that still counts as equal — use a small
// value (e.g. 2-4) to absorb GPU rounding / driver variance between backends.
// If dimensions differ the result reports sameSize=false and match()==false.
inline ImageDiff compareImages(const Image& a, const Image& b, int tolerance = 0) {
    ImageDiff d;
    if (a.width != b.width || a.height != b.height)
        return d; // sameSize stays false
    d.sameSize = true;
    if (a.rgb.empty())
        return d;

    uint64_t sumDelta          = 0;
    const std::size_t channels = a.rgb.size();
    for (std::size_t px = 0; px < channels; px += 3) {
        int dr = std::abs(static_cast<int>(a.rgb[px]) - static_cast<int>(b.rgb[px]));
        int dg = std::abs(static_cast<int>(a.rgb[px + 1]) - static_cast<int>(b.rgb[px + 1]));
        int db = std::abs(static_cast<int>(a.rgb[px + 2]) - static_cast<int>(b.rgb[px + 2]));
        sumDelta += static_cast<uint64_t>(dr) + static_cast<uint64_t>(dg) + static_cast<uint64_t>(db);
        int pixelMax = dr;
        if (dg > pixelMax)
            pixelMax = dg;
        if (db > pixelMax)
            pixelMax = db;
        if (pixelMax > d.maxChannelDelta)
            d.maxChannelDelta = pixelMax;
        if (pixelMax > tolerance)
            ++d.diffPixels;
    }
    d.meanChannelDelta = static_cast<double>(sumDelta) / static_cast<double>(channels);
    return d;
}

} // namespace ds
