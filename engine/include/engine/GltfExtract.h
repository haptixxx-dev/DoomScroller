#pragma once

#include "engine/Vertex.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

// CPU-only glTF triangle-geometry extraction (cgltf parsing, no GPU device,
// no rhi types) so the same accessor-reading logic can be shared between the
// runtime MeshLoader (engine/MeshLoader.h, which uploads the result to GPU
// buffers) and offline tools (tools/level_convert, which bakes the result
// into a .dslv MeshRecord) without the two paths silently diverging.
namespace ds::gltf {

// One triangle primitive's CPU-side geometry. Indices are always 32-bit here
// (callers needing a 16-bit GPU index buffer narrow as needed, same as
// MeshLoader::load already does for its own GPU upload).
struct ExtractedPrimitive {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Extracts every triangle primitive across every mesh in `path`, in
// data->meshes[] order — the same iteration MeshLoader::load has always
// used (it does not look at the node graph). Throws std::runtime_error on
// cgltf parse/buffer-load failure.
std::vector<ExtractedPrimitive> extractTrianglePrimitives(const std::string& path);

// A primitive plus its owning node's world transform, for offline level
// conversion (tools/level_convert) where placement matters. Any uniform
// scale on the node is baked directly into `primitive.vertices`; non-uniform
// scale/shear cannot be represented by position+rotation alone and throws
// std::runtime_error naming the offending node (callers should re-export
// with uniform/no scale on level geometry).
struct ExtractedNodePrimitive {
    ExtractedPrimitive primitive;
    glm::vec3 worldPosition{0.f};
    glm::quat worldRotation{1.f, 0.f, 0.f, 0.f};
};

// Like extractTrianglePrimitives, but walks the node graph (every node in
// the file, not just the default scene, since a level export may have no
// scene set) so each returned primitive carries its owning node's world
// transform. Throws std::runtime_error on parse failure or non-uniform
// node scale.
std::vector<ExtractedNodePrimitive> extractNodePrimitives(const std::string& path);

} // namespace ds::gltf
