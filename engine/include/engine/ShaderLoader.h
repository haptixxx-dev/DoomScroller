#pragma once

#include "engine/rhi/IRHIDevice.h"

#include <SDL3/SDL_gpu.h>

#include <filesystem>
#include <string>

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
    rhi::RHIShader load(rhi::IRHIDevice& device, const std::string& name, rhi::ShaderStage stage);

  private:
    SDL_GPUDevice* m_gpu;
    std::filesystem::path m_dir;
    rhi::ShaderFormat m_format;
    std::string m_ext;
};

} // namespace ds
