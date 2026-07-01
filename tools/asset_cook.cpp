// asset_cook — offline texture cooker that emits a .dstex container.
//
//   asset_cook <input.png> <output.dstex> [--rgba8]
//
// Loads an image (PNG/JPG/TGA/... anything stb_image decodes) as 8-bit RGBA and
// writes it into the versioned DSTX container (engine/TextureFormatDstex.h).
//
// By default the payload is BC7 block-compressed (format = 1, 16 bytes per 4x4
// block, ~4x smaller than RGBA8) via the vendored bc7enc encoder. Pass --rgba8
// to emit the uncompressed RGBA8 payload instead (format = 0) — useful for the
// DS_DEV runtime fallback / debugging, or on assets the BC7 path should skip.
//
// The BC7 block layout + edge padding lives in the pure, unit-tested
// engine/Bc7Cook.h; this tool injects the bc7enc-backed per-block compressor.
// The container layout and reader (parseDstex) are unchanged across formats.

#include "engine/Bc7Cook.h"
#include "engine/TextureFormatDstex.h"

#include <bc7enc.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <stb_image.h>
#include <string>
#include <vector>

using namespace ds;

namespace {

bool writeFile(const char* path, std::span<const uint8_t> bytes) {
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    const std::size_t wrote = bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), f);
    const bool ok           = (wrote == bytes.size());
    std::fclose(f);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    const char* inPath  = nullptr;
    const char* outPath = nullptr;
    bool useBc7         = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--rgba8") == 0) {
            useBc7 = false;
        } else if (inPath == nullptr) {
            inPath = argv[i];
        } else if (outPath == nullptr) {
            outPath = argv[i];
        }
    }
    if (inPath == nullptr || outPath == nullptr) {
        std::fprintf(stderr, "usage: %s <input.png> <output.dstex> [--rgba8]\n", argv[0]);
        return 2;
    }

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

    const auto w = static_cast<uint32_t>(width);
    const auto h = static_cast<uint32_t>(height);

    std::vector<uint8_t> payload;
    DstexFormat format = DstexFormat::RGBA8;
    if (useBc7) {
        // Initialise the bc7enc encoder once, then compress each padded 4x4
        // tile. Linear (non-perceptual) weights keep the cook deterministic and
        // suitable for both color and data textures.
        bc7enc_compress_block_init();
        bc7enc_compress_block_params params;
        bc7enc_compress_block_params_init(&params);
        bc7enc_compress_block_params_init_linear_weights(&params);

        payload = bc7Cook(
            reinterpret_cast<const uint8_t*>(pixels), w, h,
            [&params](const uint8_t tile[64], uint8_t block[16]) { bc7enc_compress_block(block, tile, &params); });
        format = DstexFormat::BC7;
    } else {
        const std::size_t rgbaBytes = static_cast<std::size_t>(w) * h * 4u;
        payload.assign(pixels, pixels + rgbaBytes);
        format = DstexFormat::RGBA8;
    }

    DstexHeader header{};
    header.width     = w;
    header.height    = h;
    header.mipLevels = 1;
    header.format    = static_cast<uint32_t>(format);
    header.dataSize  = static_cast<uint32_t>(payload.size());

    const std::vector<uint8_t> blob = serializeDstex(header, payload);
    stbi_image_free(pixels);

    if (!writeFile(outPath, blob)) {
        std::fprintf(stderr, "asset_cook: failed to write '%s'\n", outPath);
        return 1;
    }

    std::printf("asset_cook: wrote %s (%ux%u %s, %zu bytes payload)\n", outPath, w, h, useBc7 ? "BC7" : "RGBA8",
                payload.size());
    return 0;
}
