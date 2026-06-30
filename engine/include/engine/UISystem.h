#pragma once

#include "engine/rhi/IRHIDevice.h"

#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <string_view>
#include <vector>

namespace ds {

// Immediate-mode 2D overlay renderer.
//
// Call begin() once per frame (clears the batch), then issue drawQuad/drawText
// in screen pixel coordinates (origin top-left), then flush() inside an active
// render pass to upload the batched geometry and emit one draw per texture run.
//
// Solid (untextured) quads sample a built-in 1x1 white texture; text uses a
// built-in 8x8 ASCII bitmap font atlas, so the UI has no external asset
// dependency. The system owns its own pipeline, shaders, dynamic vertex
// buffer, white texture and font atlas; init() must be called once after the
// device exists, and shutdown() before the device is destroyed.
class UISystem {
  public:
    // One screen-space vertex: pixel position, uv into the bound texture, rgba.
    struct Vertex {
        glm::vec2 pos{0.f};
        glm::vec2 uv{0.f};
        glm::vec4 color{1.f};
    };

    UISystem() = default;

    // Create GPU resources. shaderDir is ds::paths::shaders(); the SDL3 device
    // handle is needed by ShaderLoader. Throws on shader load failure.
    void init(rhi::IRHIDevice& device, void* sdlGpuDevice, const std::filesystem::path& shaderDir);
    void shutdown(rhi::IRHIDevice& device);

    // Per-frame lifecycle. screenW/H drive the pixel->NDC push constant.
    void begin(int screenW, int screenH);

    // Solid colored quad (uses the built-in white texture).
    void drawQuad(float x, float y, float w, float h, const glm::vec4& color);

    // Textured quad sampling the full 0..1 uv range of `texture`.
    void drawTexturedQuad(float x, float y, float w, float h, const glm::vec4& color, rhi::RHITexture texture,
                          rhi::RHISampler sampler);

    // 8x8 bitmap text. `scale` multiplies the 8px glyph cell; advance is 8*scale
    // per character. Newlines move down one line.
    void drawText(float x, float y, std::string_view text, float scale, const glm::vec4& color);

    // Pixel width/height of a string at the given scale (no wrapping).
    float textWidth(std::string_view text, float scale) const;
    float textHeight(std::string_view text, float scale) const;

    // Upload the batch and record draws into the active render pass.
    void flush(rhi::IRHIDevice& device, rhi::IRHICommandList* cmd);

  private:
    // A contiguous run of vertices sharing one texture/sampler -> one draw.
    struct Batch {
        rhi::RHITexture texture{};
        rhi::RHISampler sampler{};
        uint32_t first = 0; // first vertex
        uint32_t count = 0; // vertex count
    };

    void pushQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, const glm::vec4& color,
                  rhi::RHITexture texture, rhi::RHISampler sampler);

    static constexpr uint32_t kMaxVerts = 64 * 1024; // 6 verts/quad

    rhi::RHIShader m_vs       = {};
    rhi::RHIShader m_fs       = {};
    rhi::RHIPipeline m_pipe   = {};
    rhi::RHIBuffer m_vbo      = {};
    rhi::RHITexture m_white   = {};
    rhi::RHITexture m_font    = {};
    rhi::RHISampler m_sampler = {};

    std::vector<Vertex> m_verts;
    std::vector<Batch> m_batches;

    int m_screenW = 1;
    int m_screenH = 1;
};

} // namespace ds
