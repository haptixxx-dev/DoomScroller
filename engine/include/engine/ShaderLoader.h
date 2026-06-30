#pragma once

#include "engine/rhi/IRHIDevice.h"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ds {

// Detects supported shader format from the SDL3 GPU device and loads
// the matching compiled bytecode from the shader output directory.
//
// Files expected:  <name>.<stage>.spv   (SPIRV / Vulkan)
//                  <name>.<stage>.msl   (Metal)
//                  <name>.<stage>.dxil  (D3D12)
class ShaderLoader {
  public:
    explicit ShaderLoader(SDL_GPUDevice* device, std::filesystem::path shaderDir);

    // Loads and creates a shader. Throws on failure.
    rhi::RHIShader load(rhi::IRHIDevice& device, const std::string& name, rhi::ShaderStage stage,
                        uint32_t numSamplers = 0, uint32_t numUniformBuffers = 0);

    // Raw compiled bytecode for a shader stage (task 39: compute pipeline create
    // needs the bytecode directly, not a graphics RHIShader). For MSL the buffer
    // is null-terminated. Returns false (and leaves outBytes empty) if the file
    // is missing, so the caller can fall back gracefully rather than throw.
    bool loadBytecode(const std::string& name, rhi::ShaderStage stage, std::vector<uint8_t>& outBytes) const;

    // The shader format / bytecode the device selected (SPIRV / MSL / DXIL).
    rhi::ShaderFormat format() const { return m_format; }

  private:
    SDL_GPUDevice* m_gpu;
    std::filesystem::path m_dir;
    rhi::ShaderFormat m_format;
    std::string m_ext;
};

} // namespace ds
