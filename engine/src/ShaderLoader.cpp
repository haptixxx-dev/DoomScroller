#include "engine/ShaderLoader.h"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace ds {

ShaderLoader::ShaderLoader(SDL_GPUDevice* device, std::filesystem::path shaderDir)
    : m_gpu(device), m_dir(std::move(shaderDir)) {
    SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(m_gpu);

    if (supported & SDL_GPU_SHADERFORMAT_MSL) {
        m_format = rhi::ShaderFormat::MSL;
        m_ext    = "msl";
    } else if (supported & SDL_GPU_SHADERFORMAT_SPIRV) {
        m_format = rhi::ShaderFormat::SPIRV;
        m_ext    = "spv";
    } else if (supported & SDL_GPU_SHADERFORMAT_DXIL) {
        m_format = rhi::ShaderFormat::DXIL;
        m_ext    = "dxil";
    } else {
        throw std::runtime_error("ShaderLoader: no supported shader format");
    }
}

rhi::RHIShader ShaderLoader::load(rhi::IRHIDevice& device, const std::string& name, rhi::ShaderStage stage,
                                  uint32_t numSamplers, uint32_t numUniformBuffers) {
    const char* stageStr = stage == rhi::ShaderStage::Vertex     ? "vertex"
                           : stage == rhi::ShaderStage::Fragment ? "fragment"
                                                                 : "compute";

    std::filesystem::path path = m_dir / (name + "." + stageStr + "." + m_ext);

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("ShaderLoader: cannot open " + path.string());

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    // MSL is null-terminated text; SPIRV/DXIL are binary
    bool isMSL = (m_format == rhi::ShaderFormat::MSL);
    std::vector<char> buf(isMSL ? size + 1 : size);
    if (!file.read(buf.data(), size))
        throw std::runtime_error("ShaderLoader: read failed " + path.string());
    if (isMSL)
        buf[size] = '\0';

    rhi::ShaderDesc desc{};
    desc.stage             = stage;
    desc.format            = m_format;
    desc.bytecode          = buf.data();
    desc.bytecodeSize      = static_cast<size_t>(isMSL ? size + 1 : size);
    desc.entryPoint        = stage == rhi::ShaderStage::Vertex ? "vertMain" : "fragMain";
    desc.numSamplers       = numSamplers;
    desc.numUniformBuffers = numUniformBuffers;

    return device.createShader(desc);
}

} // namespace ds
