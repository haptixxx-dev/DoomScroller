#include "engine/UISystem.h"

#include "engine/ShaderLoader.h"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <filesystem>

namespace ds {

// ---------------------------------------------------------------------------
// Built-in 8x8 ASCII bitmap font (public domain "font8x8_basic" subset).
//
// One entry per printable glyph, ASCII 32..126 (95 glyphs). Each glyph is 8
// rows; each row is a byte whose bits, LSB-first, are the 8 columns left to
// right (bit set = lit pixel). The atlas is built as a 16-wide grid of 8x8
// cells.
// ---------------------------------------------------------------------------
namespace {

constexpr int kFirstGlyph = 32;  // space
constexpr int kLastGlyph  = 126; // '~'
constexpr int kGlyphCount = kLastGlyph - kFirstGlyph + 1;
constexpr int kCellPx     = 8;
constexpr int kAtlasCols  = 16;
constexpr int kAtlasRows  = (kGlyphCount + kAtlasCols - 1) / kAtlasCols; // 6
constexpr int kAtlasW     = kAtlasCols * kCellPx;                        // 128
constexpr int kAtlasH     = kAtlasRows * kCellPx;                        // 48

// font8x8_basic rows, indexed by (glyph - 32). LSB = leftmost column.
constexpr unsigned char kFont8x8[kGlyphCount][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ' '
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // '!'
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // '"'
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // '#'
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // '$'
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // '%'
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // '&'
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // '\''
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // '('
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // ')'
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // '*'
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // '+'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // ','
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // '-'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // '.'
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // '/'
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // '0'
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // '1'
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // '2'
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // '3'
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // '4'
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // '5'
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // '6'
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // '7'
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // '8'
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // '9'
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // ':'
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // ';'
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // '<'
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // '='
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // '>'
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // '?'
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // '@'
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // 'A'
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // 'B'
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // 'C'
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // 'D'
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, // 'E'
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, // 'F'
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // 'G'
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // 'H'
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 'I'
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // 'J'
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // 'K'
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, // 'L'
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // 'M'
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // 'N'
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // 'O'
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, // 'P'
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // 'Q'
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // 'R'
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // 'S'
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 'T'
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // 'U'
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // 'V'
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // 'W'
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // 'X'
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // 'Y'
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // 'Z'
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // '['
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // '\'
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // ']'
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // '^'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // '_'
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // '`'
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // 'a'
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // 'b'
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // 'c'
    {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00}, // 'd'
    {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00}, // 'e'
    {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00}, // 'f'
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // 'g'
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // 'h'
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 'i'
    {0x30, 0x00, 0x38, 0x30, 0x30, 0x33, 0x33, 0x1E}, // 'j'
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // 'k'
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 'l'
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // 'm'
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // 'n'
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // 'o'
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // 'p'
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // 'q'
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // 'r'
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // 's'
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // 't'
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // 'u'
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // 'v'
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // 'w'
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // 'x'
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // 'y'
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // 'z'
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // '{'
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // '|'
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // '}'
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // '~'
};

} // namespace

void UISystem::init(rhi::IRHIDevice& device, void* sdlGpuDevice, const std::filesystem::path& shaderDir) {
    ShaderLoader loader(static_cast<SDL_GPUDevice*>(sdlGpuDevice), shaderDir);
    m_vs = loader.load(device, "ui", rhi::ShaderStage::Vertex, 0, 1);
    m_fs = loader.load(device, "ui", rhi::ShaderStage::Fragment, 1, 0);

    // Nearest sampling keeps both the white texel and the crisp bitmap font
    // pixel-sharp at any UI scale.
    rhi::SamplerDesc sd{};
    sd.minFilter = rhi::FilterMode::Nearest;
    sd.magFilter = rhi::FilterMode::Nearest;
    sd.mipFilter = rhi::FilterMode::Nearest;
    sd.addressU  = rhi::AddressMode::ClampToEdge;
    sd.addressV  = rhi::AddressMode::ClampToEdge;
    sd.addressW  = rhi::AddressMode::ClampToEdge;
    m_sampler    = device.createSampler(sd);

    // 1x1 opaque white for solid quads.
    {
        const uint8_t whitePx[4] = {255, 255, 255, 255};
        rhi::TextureDesc td{};
        td.width     = 1;
        td.height    = 1;
        td.format    = rhi::TextureFormat::RGBA8Unorm;
        td.debugName = "ui_white";
        m_white      = device.createTexture(td);
        device.uploadImmediateTexture(m_white, whitePx, sizeof(whitePx), 1, 1);
    }

    // Build the font atlas: white RGBA with alpha taken from the glyph bitmap so
    // glyphs blend over the background and tint by the vertex color.
    {
        std::vector<uint8_t> atlas(static_cast<size_t>(kAtlasW) * kAtlasH * 4, 0);
        for (int g = 0; g < kGlyphCount; ++g) {
            int cellX = (g % kAtlasCols) * kCellPx;
            int cellY = (g / kAtlasCols) * kCellPx;
            for (int row = 0; row < kCellPx; ++row) {
                unsigned char bits = kFont8x8[g][row];
                for (int col = 0; col < kCellPx; ++col) {
                    bool lit   = (bits >> col) & 1u;
                    int px     = cellX + col;
                    int py     = cellY + row;
                    uint8_t* p = atlas.data() + (static_cast<size_t>(py) * kAtlasW + px) * 4;
                    p[0] = p[1] = p[2] = 255;
                    p[3]               = lit ? 255 : 0;
                }
            }
        }
        rhi::TextureDesc td{};
        td.width     = kAtlasW;
        td.height    = kAtlasH;
        td.format    = rhi::TextureFormat::RGBA8Unorm;
        td.debugName = "ui_font";
        m_font       = device.createTexture(td);
        device.uploadImmediateTexture(m_font, atlas.data(), static_cast<uint64_t>(atlas.size()), kAtlasW, kAtlasH);
    }

    // Vertex layout: vec2 pos, vec2 uv, vec4 color.
    static rhi::VertexAttribute attrs[3];
    attrs[0].location     = 0;
    attrs[0].binding      = 0;
    attrs[0].offset       = offsetof(Vertex, pos);
    attrs[0].elementCount = 2;
    attrs[0].isFloat      = true;
    attrs[1].location     = 1;
    attrs[1].binding      = 0;
    attrs[1].offset       = offsetof(Vertex, uv);
    attrs[1].elementCount = 2;
    attrs[1].isFloat      = true;
    attrs[2].location     = 2;
    attrs[2].binding      = 0;
    attrs[2].offset       = offsetof(Vertex, color);
    attrs[2].elementCount = 4;
    attrs[2].isFloat      = true;

    static rhi::VertexBinding binding{};
    binding.binding   = 0;
    binding.stride    = static_cast<uint32_t>(sizeof(Vertex));
    binding.instanced = false;

    // Straight alpha blend over the 3D scene; no depth, no culling.
    rhi::ColorTargetDesc colorTarget{};
    colorTarget.format             = device.swapchainFormat();
    colorTarget.blend.blendEnabled = true;
    colorTarget.blend.srcColor     = rhi::BlendFactor::SrcAlpha;
    colorTarget.blend.dstColor     = rhi::BlendFactor::OneMinusSrcAlpha;
    colorTarget.blend.colorOp      = rhi::BlendOp::Add;
    colorTarget.blend.srcAlpha     = rhi::BlendFactor::One;
    colorTarget.blend.dstAlpha     = rhi::BlendFactor::OneMinusSrcAlpha;
    colorTarget.blend.alphaOp      = rhi::BlendOp::Add;

    rhi::PipelineDesc pipeDesc{};
    pipeDesc.vertexShader     = m_vs;
    pipeDesc.fragmentShader   = m_fs;
    pipeDesc.vertexAttributes = {attrs, 3};
    pipeDesc.vertexBindings   = {&binding, 1};
    pipeDesc.colorTargets     = {&colorTarget, 1};
    pipeDesc.hasDepth         = false;
    pipeDesc.depthTest        = false;
    pipeDesc.depthWrite       = false;
    pipeDesc.cullMode         = rhi::CullMode::None;
    pipeDesc.topology         = rhi::PrimitiveTopology::TriangleList;
    m_pipe                    = device.createPipeline(pipeDesc);

    rhi::BufferDesc vbd{};
    vbd.size      = sizeof(Vertex) * kMaxVerts;
    vbd.usage     = rhi::BufferUsage::Vertex;
    vbd.debugName = "ui_vertices";
    m_vbo         = device.createBuffer(vbd);

    m_verts.reserve(kMaxVerts);
}

void UISystem::shutdown(rhi::IRHIDevice& device) {
    if (m_vbo.valid())
        device.destroyBuffer(m_vbo);
    if (m_pipe.valid())
        device.destroyPipeline(m_pipe);
    if (m_vs.valid())
        device.destroyShader(m_vs);
    if (m_fs.valid())
        device.destroyShader(m_fs);
    if (m_white.valid())
        device.destroyTexture(m_white);
    if (m_font.valid())
        device.destroyTexture(m_font);
    if (m_sampler.valid())
        device.destroySampler(m_sampler);
}

void UISystem::begin(int screenW, int screenH) {
    m_screenW = screenW > 0 ? screenW : 1;
    m_screenH = screenH > 0 ? screenH : 1;
    m_verts.clear();
    m_batches.clear();
}

void UISystem::pushQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                        const glm::vec4& color, rhi::RHITexture texture, rhi::RHISampler sampler) {
    if (m_verts.size() + 6 > kMaxVerts)
        return;

    // Extend the current batch if it shares the same texture+sampler, else open
    // a new one. Adjacent solid quads coalesce into a single draw.
    if (m_batches.empty() || m_batches.back().texture.ptr != texture.ptr ||
        m_batches.back().sampler.ptr != sampler.ptr) {
        Batch b{};
        b.texture = texture;
        b.sampler = sampler;
        b.first   = static_cast<uint32_t>(m_verts.size());
        b.count   = 0;
        m_batches.push_back(b);
    }

    const float x1 = x + w, y1 = y + h;
    Vertex v0v{{x, y}, {u0, v0}, color};
    Vertex v1v{{x1, y}, {u1, v0}, color};
    Vertex v2v{{x1, y1}, {u1, v1}, color};
    Vertex v3v{{x, y1}, {u0, v1}, color};

    m_verts.push_back(v0v);
    m_verts.push_back(v1v);
    m_verts.push_back(v2v);
    m_verts.push_back(v0v);
    m_verts.push_back(v2v);
    m_verts.push_back(v3v);
    m_batches.back().count += 6;
}

void UISystem::drawQuad(float x, float y, float w, float h, const glm::vec4& color) {
    pushQuad(x, y, w, h, 0.f, 0.f, 1.f, 1.f, color, m_white, m_sampler);
}

void UISystem::drawTexturedQuad(float x, float y, float w, float h, const glm::vec4& color, rhi::RHITexture texture,
                                rhi::RHISampler sampler) {
    pushQuad(x, y, w, h, 0.f, 0.f, 1.f, 1.f, color, texture, sampler);
}

void UISystem::drawText(float x, float y, std::string_view text, float scale, const glm::vec4& color) {
    const float cell = kCellPx * scale;
    const float du   = static_cast<float>(kCellPx) / kAtlasW;
    const float dv   = static_cast<float>(kCellPx) / kAtlasH;
    float cursorX    = x;
    float cursorY    = y;
    for (char ch : text) {
        if (ch == '\n') {
            cursorX = x;
            cursorY += cell;
            continue;
        }
        int code = static_cast<unsigned char>(ch);
        if (code >= kFirstGlyph && code <= kLastGlyph && ch != ' ') {
            int g     = code - kFirstGlyph;
            int cellX = g % kAtlasCols;
            int cellY = g / kAtlasCols;
            float u0  = cellX * du;
            float v0  = cellY * dv;
            pushQuad(cursorX, cursorY, cell, cell, u0, v0, u0 + du, v0 + dv, color, m_font, m_sampler);
        }
        cursorX += cell;
    }
}

float UISystem::textWidth(std::string_view text, float scale) const {
    float maxW = 0.f, cur = 0.f;
    for (char ch : text) {
        if (ch == '\n') {
            cur = 0.f;
            continue;
        }
        cur += kCellPx * scale;
        if (cur > maxW)
            maxW = cur;
    }
    return maxW;
}

float UISystem::textHeight(std::string_view text, float scale) const {
    int lines = 1;
    for (char ch : text)
        if (ch == '\n')
            ++lines;
    return lines * kCellPx * scale;
}

void UISystem::flush(rhi::IRHIDevice& device, rhi::IRHICommandList* cmd) {
    if (m_verts.empty())
        return;

    device.uploadImmediate(m_vbo, m_verts.data(), m_verts.size() * sizeof(Vertex), 0);

    cmd->setPipeline(m_pipe);

    struct Push {
        glm::vec2 invScreen;
        glm::vec2 pad{0.f};
    } push;
    push.invScreen = {2.f / static_cast<float>(m_screenW), 2.f / static_cast<float>(m_screenH)};
    cmd->pushVertexConstants(&push, sizeof(push));

    cmd->setVertexBuffer(0, m_vbo, 0);

    for (const auto& b : m_batches) {
        if (b.count == 0)
            continue;
        cmd->bindFragmentTexture(0, b.texture, b.sampler);
        cmd->draw(b.count, 1, b.first, 0);
    }
}

} // namespace ds
