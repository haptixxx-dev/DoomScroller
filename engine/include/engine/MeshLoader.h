#pragma once

#include "engine/ecs/Components.h"
#include "engine/rhi/IRHIDevice.h"

#include <string>
#include <vector>

namespace ds::MeshLoader {

// Loads all triangle primitives from a .glb/.gltf file.
// Returns one MeshComponent per primitive. Caller owns the GPU buffers.
std::vector<MeshComponent> load(rhi::IRHIDevice& device, const std::string& path);

} // namespace ds::MeshLoader
