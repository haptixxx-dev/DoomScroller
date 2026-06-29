#include <catch2/catch_test_macros.hpp>
#include "engine/rhi/RHITypes.h"

using namespace ds::rhi;

TEST_CASE("Handle validity", "[rhi]") {
    RHIBuffer buf{};
    REQUIRE(!buf.valid());

    buf.ptr = reinterpret_cast<void*>(0x1);
    REQUIRE(buf.valid());

    RHITexture tex{};
    REQUIRE(!tex.valid());

    RHIPipeline pipe{};
    REQUIRE(!pipe.valid());
}

TEST_CASE("ShaderDesc defaults", "[rhi]") {
    ShaderDesc desc{};
    REQUIRE(desc.stage        == ShaderStage::Vertex);
    REQUIRE(desc.format       == ShaderFormat::SPIRV);
    REQUIRE(desc.bytecode     == nullptr);
    REQUIRE(desc.bytecodeSize == 0);
    REQUIRE(desc.numSamplers  == 0);
    REQUIRE(desc.numUniformBuffers  == 0);
    REQUIRE(desc.numStorageBuffers  == 0);
    REQUIRE(desc.numStorageTextures == 0);
}

TEST_CASE("BufferUsage bitfield OR", "[rhi]") {
    BufferUsage combined = BufferUsage::Vertex | BufferUsage::Index;
    uint32_t raw = static_cast<uint32_t>(combined);
    REQUIRE(raw & static_cast<uint32_t>(BufferUsage::Vertex));
    REQUIRE(raw & static_cast<uint32_t>(BufferUsage::Index));
    REQUIRE(!(raw & static_cast<uint32_t>(BufferUsage::Uniform)));
}

TEST_CASE("PipelineDesc defaults", "[rhi]") {
    PipelineDesc desc{};
    REQUIRE(desc.hasDepth   == true);
    REQUIRE(desc.depthTest  == true);
    REQUIRE(desc.depthWrite == true);
    REQUIRE(desc.cullMode   == CullMode::Back);
    REQUIRE(desc.fillMode   == FillMode::Solid);
    REQUIRE(desc.topology   == PrimitiveTopology::TriangleList);
    REQUIRE(desc.depthCompare == CompareOp::Less);
}
