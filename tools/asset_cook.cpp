// asset_cook — offline texture cooker that emits a .dstex container.
//
//   asset_cook <input.png> <output.dstex>
//
// Loads an image (PNG/JPG/TGA/... anything stb_image decodes) as 8-bit RGBA and
// writes it into the versioned DSTX container (engine/TextureFormatDstex.h) as an
// uncompressed RGBA8 payload (format = 0).
//
// BC7 SEAM (documented TODO)
// --------------------------
// The intended cooked form is BC7 block-compressed (format = 1, 16 bytes per
// 4x4 block, ~4x smaller than RGBA8). A real BC7 encoder (e.g. bc7enc/ispc) is a
// heavy external we deliberately do NOT pull into the build yet. Until that
// submodule lands, the cook emits the RGBA8 payload unchanged — a valid first
// cut the engine can already consume. When the encoder is added, compress the
// RGBA8 pixels to a BC7 block stream here and set header.format = BC7; the
// container layout and reader (parseDstex) need no change.

#include "engine/TextureFormatDstex.h"

#include <stb_image.h>

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

using namespace ds;

namespace {

bool writeFile(const char* path, std::span<const uint8_t> bytes) {
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    const std::size_t wrote = bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), f);
    const bool ok          = (wrote == bytes.size());
    std::fclose(f);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <input.png> <output.dstex>\n", argv[0]);
        return 2;
    }

    const char* inPath  = argv[1];
    const char* outPath = argv[2];

    int width    = 0;
    int height   = 0;
    int channels = 0;
    // Force 4 channels (RGBA8) regardless of the source's channel count.
    stbi_uc* pixels = stbi_load(inPath, &width, &height, &channels, 4);
    if (pixels == nullptr) {
        std::fprintf(stderr, "asset_cook: failed to load '%s': %s\n", inPath, stbi_failure_reason());
        return 1;
    }
    if (width <= 0 || height <= 0) {
        std::fprintf(stderr, "asset_cook: '%s' has invalid dimensions %dx%d\n", inPath, width, height);
        stbi_image_free(pixels);
        return 1;
    }

    const std::size_t payloadBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    std::span<const uint8_t> payload{reinterpret_cast<const uint8_t*>(pixels), payloadBytes};

    DstexHeader header{};
    header.width     = static_cast<uint32_t>(width);
    header.height    = static_cast<uint32_t>(height);
    header.mipLevels = 1;
    header.format    = static_cast<uint32_t>(DstexFormat::RGBA8);
    header.dataSize  = static_cast<uint32_t>(payloadBytes);

    const std::vector<uint8_t> blob = serializeDstex(header, payload);
    stbi_image_free(pixels);

    if (!writeFile(outPath, blob)) {
        std::fprintf(stderr, "asset_cook: failed to write '%s'\n", outPath);
        return 1;
    }

    std::printf("asset_cook: wrote %s (%dx%d RGBA8, %zu bytes payload)\n", outPath, width, height, payloadBytes);
    return 0;
}
