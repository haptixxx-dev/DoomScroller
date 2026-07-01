#include "engine/GltfExtract.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <cgltf.h>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <stdexcept>
#include <utility>

namespace ds::gltf {

namespace {

// Parses + loads buffers for `path`, throwing std::runtime_error on failure.
// Caller owns the returned cgltf_data* and must cgltf_free it.
cgltf_data* parseFile(const std::string& path) {
    cgltf_options opts{};
    cgltf_data* data = nullptr;

    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success)
        throw std::runtime_error("cgltf_parse_file failed: " + path);

    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("cgltf_load_buffers failed: " + path);
    }
    return data;
}

// Extracts every triangle primitive of one cgltf_mesh. Shared by both
// extractTrianglePrimitives (mesh-array order) and extractNodePrimitives
// (node-graph order) so the accessor-reading logic lives in exactly one
// place.
std::vector<ExtractedPrimitive> extractMeshPrimitives(const cgltf_mesh& mesh) {
    std::vector<ExtractedPrimitive> out;

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

        ExtractedPrimitive ep;
        cgltf_size vertCount = posAcc->count;
        ep.vertices.resize(vertCount);

        float tmp[3];
        for (cgltf_size vi = 0; vi < vertCount; ++vi) {
            cgltf_accessor_read_float(posAcc, vi, tmp, 3);
            ep.vertices[vi].pos   = {tmp[0], tmp[1], tmp[2]};
            ep.vertices[vi].color = {1.f, 1.f, 1.f};
            if (uvAcc) {
                cgltf_accessor_read_float(uvAcc, vi, tmp, 2);
                ep.vertices[vi].uv = {tmp[0], tmp[1]};
            }
            if (normalAcc) {
                cgltf_accessor_read_float(normalAcc, vi, tmp, 3);
                ep.vertices[vi].normal = {tmp[0], tmp[1], tmp[2]};
            }
        }

        const cgltf_accessor* idxAcc = prim.indices;
        cgltf_size idxCount          = idxAcc->count;
        ep.indices.resize(idxCount);
        for (cgltf_size ii = 0; ii < idxCount; ++ii)
            ep.indices[ii] = static_cast<uint32_t>(cgltf_accessor_read_index(idxAcc, ii));

        out.push_back(std::move(ep));
    }

    return out;
}

} // namespace

std::vector<ExtractedPrimitive> extractTrianglePrimitives(const std::string& path) {
    cgltf_data* data = parseFile(path);

    std::vector<ExtractedPrimitive> out;
    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        auto prims = extractMeshPrimitives(data->meshes[mi]);
        for (auto& p : prims)
            out.push_back(std::move(p));
    }

    cgltf_free(data);
    return out;
}

std::vector<ExtractedNodePrimitive> extractNodePrimitives(const std::string& path) {
    cgltf_data* data = parseFile(path);

    std::vector<ExtractedNodePrimitive> out;
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& node = data->nodes[ni];
        if (!node.mesh)
            continue;

        float m[16];
        cgltf_node_transform_world(&node, m);
        glm::mat4 world = glm::make_mat4(m);

        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        if (!glm::decompose(world, scale, rotation, translation, skew, perspective)) {
            cgltf_free(data);
            throw std::runtime_error(std::string("glTF node '") + (node.name ? node.name : "<unnamed>") +
                                     "' has a degenerate world transform that cannot be decomposed");
        }

        constexpr float kScaleEps = 1e-4f;
        if (std::abs(scale.x - scale.y) > kScaleEps || std::abs(scale.y - scale.z) > kScaleEps) {
            cgltf_free(data);
            throw std::runtime_error(std::string("glTF node '") + (node.name ? node.name : "<unnamed>") +
                                     "' has non-uniform scale/shear; re-export level geometry with uniform scale");
        }

        auto prims = extractMeshPrimitives(*node.mesh);
        for (auto& p : prims) {
            // Bake the node's uniform scale directly into local-space
            // vertex positions so MeshRecordHeader never needs a scale field.
            if (std::abs(scale.x - 1.f) > kScaleEps) {
                for (auto& v : p.vertices)
                    v.pos *= scale.x;
            }
            ExtractedNodePrimitive np;
            np.primitive     = std::move(p);
            np.worldPosition = translation;
            np.worldRotation = rotation;
            out.push_back(std::move(np));
        }
    }

    cgltf_free(data);
    return out;
}

} // namespace ds::gltf
