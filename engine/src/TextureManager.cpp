#include "engine/TextureManager.h"

#include <stb_image.h>
#include <stdexcept>

namespace ds {

TextureManager::TextureManager(rhi::IRHIDevice& device) : m_device(device) {}

TextureManager::~TextureManager() {
    destroyAll();
}

rhi::RHITexture TextureManager::load(const std::filesystem::path& path) {
    std::string key = path.string();
    auto it         = m_cache.find(key);
    if (it != m_cache.end())
        return it->second;

    int w, h, channels;
    stbi_uc* pixels = stbi_load(key.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels)
        throw std::runtime_error("TextureManager: failed to load " + key);

    rhi::RHITexture tex = createFromMemory(pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h), key.c_str());
    stbi_image_free(pixels);

    m_cache.emplace(std::move(key), tex);
    return tex;
}

rhi::RHITexture TextureManager::createFromMemory(const uint8_t* rgba, uint32_t width, uint32_t height,
                                                 const char* debugName) {
    rhi::TextureDesc desc{};
    desc.width     = width;
    desc.height    = height;
    desc.format    = rhi::TextureFormat::RGBA8Unorm;
    desc.debugName = debugName;

    rhi::RHITexture tex = m_device.createTexture(desc);
    m_device.uploadImmediateTexture(tex, rgba, static_cast<uint64_t>(width) * height * 4, width, height);
    return tex;
}

void TextureManager::destroy(rhi::RHITexture texture) {
    m_device.destroyTexture(texture);
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->second.ptr == texture.ptr) {
            m_cache.erase(it);
            return;
        }
    }
}

void TextureManager::destroyAll() {
    for (auto& [key, tex] : m_cache)
        m_device.destroyTexture(tex);
    m_cache.clear();
}

} // namespace ds
