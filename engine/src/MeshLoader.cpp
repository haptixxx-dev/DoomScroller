#include "engine/MeshLoader.h"

#include "engine/GltfExtract.h"
#include "engine/Vertex.h"

#include <vector>

namespace ds::MeshLoader {

std::vector<MeshComponent> load(rhi::IRHIDevice& device, const std::string& path) {
    std::vector<gltf::ExtractedPrimitive> prims = gltf::extractTrianglePrimitives(path);

    std::vector<MeshComponent> out;
    out.reserve(prims.size());

    for (const auto& ep : prims) {
        rhi::BufferDesc vbDesc{};
        vbDesc.size  = ep.vertices.size() * sizeof(Vertex);
        vbDesc.usage = rhi::BufferUsage::Vertex;
        auto vb      = device.createBuffer(vbDesc);
        device.uploadImmediate(vb, ep.vertices.data(), vbDesc.size);

        const size_t idxCount = ep.indices.size();
        bool needs32          = false;
        for (uint32_t idx : ep.indices) {
            if (idx > 0xFFFFu) {
                needs32 = true;
                break;
            }
        }

        rhi::BufferDesc ibDesc{};
        ibDesc.usage = rhi::BufferUsage::Index;

        MeshComponent mc{};
        mc.vertexBuffer = vb;
        mc.indexCount   = static_cast<uint32_t>(idxCount);

        if (needs32) {
            ibDesc.size    = idxCount * sizeof(uint32_t);
            mc.indexType   = rhi::IndexType::Uint32;
            mc.indexBuffer = device.createBuffer(ibDesc);
            device.uploadImmediate(mc.indexBuffer, ep.indices.data(), ibDesc.size);
        } else {
            std::vector<uint16_t> indices16(idxCount);
            for (size_t i = 0; i < idxCount; ++i)
                indices16[i] = static_cast<uint16_t>(ep.indices[i]);
            ibDesc.size    = idxCount * sizeof(uint16_t);
            mc.indexType   = rhi::IndexType::Uint16;
            mc.indexBuffer = device.createBuffer(ibDesc);
            device.uploadImmediate(mc.indexBuffer, indices16.data(), ibDesc.size);
        }

        out.push_back(mc);
    }

    return out;
}

} // namespace ds::MeshLoader
