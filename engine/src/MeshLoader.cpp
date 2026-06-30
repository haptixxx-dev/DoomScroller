#include "engine/MeshLoader.h"

#include "engine/Vertex.h"

#include <cgltf.h>
#include <stdexcept>
#include <vector>

namespace ds::MeshLoader {

std::vector<MeshComponent> load(rhi::IRHIDevice& device, const std::string& path) {
    cgltf_options opts{};
    cgltf_data* data = nullptr;

    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success)
        throw std::runtime_error("cgltf_parse_file failed: " + path);

    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("cgltf_load_buffers failed: " + path);
    }

    std::vector<MeshComponent> out;

    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles || !prim.indices)
                continue;

            const cgltf_accessor* posAcc    = nullptr;
            const cgltf_accessor* normalAcc = nullptr;
            const cgltf_accessor* uvAcc     = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute& a = prim.attributes[ai];
                if (a.type == cgltf_attribute_type_position)
                    posAcc = a.data;
                else if (a.type == cgltf_attribute_type_normal)
                    normalAcc = a.data;
                else if (a.type == cgltf_attribute_type_texcoord && a.index == 0)
                    uvAcc = a.data;
            }
            if (!posAcc)
                continue;

            cgltf_size vertCount = posAcc->count;
            std::vector<Vertex> verts(vertCount);

            float tmp[3];
            for (cgltf_size vi = 0; vi < vertCount; ++vi) {
                cgltf_accessor_read_float(posAcc, vi, tmp, 3);
                verts[vi].pos   = {tmp[0], tmp[1], tmp[2]};
                verts[vi].color = {1.f, 1.f, 1.f};
                if (uvAcc) {
                    cgltf_accessor_read_float(uvAcc, vi, tmp, 2);
                    verts[vi].uv = {tmp[0], tmp[1]};
                }
                if (normalAcc) {
                    cgltf_accessor_read_float(normalAcc, vi, tmp, 3);
                    verts[vi].normal = {tmp[0], tmp[1], tmp[2]};
                }
            }

            const cgltf_accessor* idxAcc = prim.indices;
            cgltf_size idxCount          = idxAcc->count;

            bool needs32 = false;
            for (cgltf_size ii = 0; ii < idxCount && !needs32; ++ii) {
                if (cgltf_accessor_read_index(idxAcc, ii) > 0xFFFFu)
                    needs32 = true;
            }

            rhi::BufferDesc vbDesc{};
            vbDesc.size  = vertCount * sizeof(Vertex);
            vbDesc.usage = rhi::BufferUsage::Vertex;
            auto vb      = device.createBuffer(vbDesc);
            device.uploadImmediate(vb, verts.data(), vbDesc.size);

            rhi::BufferDesc ibDesc{};
            ibDesc.usage = rhi::BufferUsage::Index;

            MeshComponent mc{};
            mc.vertexBuffer = vb;
            mc.indexCount   = static_cast<uint32_t>(idxCount);

            if (needs32) {
                std::vector<uint32_t> indices(idxCount);
                for (cgltf_size ii = 0; ii < idxCount; ++ii)
                    indices[ii] = static_cast<uint32_t>(cgltf_accessor_read_index(idxAcc, ii));
                ibDesc.size    = idxCount * sizeof(uint32_t);
                mc.indexType   = rhi::IndexType::Uint32;
                mc.indexBuffer = device.createBuffer(ibDesc);
                device.uploadImmediate(mc.indexBuffer, indices.data(), ibDesc.size);
            } else {
                std::vector<uint16_t> indices(idxCount);
                for (cgltf_size ii = 0; ii < idxCount; ++ii)
                    indices[ii] = static_cast<uint16_t>(cgltf_accessor_read_index(idxAcc, ii));
                ibDesc.size    = idxCount * sizeof(uint16_t);
                mc.indexType   = rhi::IndexType::Uint16;
                mc.indexBuffer = device.createBuffer(ibDesc);
                device.uploadImmediate(mc.indexBuffer, indices.data(), ibDesc.size);
            }

            out.push_back(mc);
        }
    }

    cgltf_free(data);
    return out;
}

} // namespace ds::MeshLoader
