#pragma once

#include "engine/rhi/IRHIDevice.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace ds {

// Loads RGBA8 images from disk via stb_image and uploads them to the GPU.
// Caches by absolute path; same path returns the same RHITexture handle.
// Owns all created textures; call destroy() or let the destructor release them.
class TextureManager {
  public:
    explicit TextureManager(rhi::IRHIDevice& device);
    ~TextureManager();

    TextureManager(const TextureManager&)            = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    // Load from file. Returns invalid handle on failure.
    rhi::RHITexture load(const std::filesystem::path& path);

    // Create from raw RGBA8 pixels (width * height * 4 bytes). Not cached.
    rhi::RHITexture createFromMemory(const uint8_t* rgba, uint32_t width, uint32_t height,
                                     const char* debugName = nullptr);

    void destroy(rhi::RHITexture texture);
    void destroyAll();

  private:
    rhi::IRHIDevice& m_device;
    std::unordered_map<std::string, rhi::RHITexture> m_cache;
};

} // namespace ds
