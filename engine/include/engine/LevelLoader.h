#pragma once

#include "engine/LevelFormat.h"
#include "engine/rhi/RHITypes.h"

#include <entt/entt.hpp>
#include <filesystem>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace ds {

class PhysicsWorld;
namespace rhi {
class IRHIDevice;
}

// In-memory form of one MeshRecord: the fixed header plus its variable-length
// local-space vertex/index payload (see LevelFormat.h's MeshRecordHeader).
struct MeshRecord {
    MeshRecordHeader header{};
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// LevelData is the in-memory form of a parsed .dslv file: the header plus the
// record blocks. Used by both the runtime loader and the offline converter
// tool (LevelLoader::write / LevelLoader::read), and by Lua-driven procedural
// level generation (engine/LevelGen.h), which builds a LevelData in memory
// without ever serializing it.
struct LevelData {
    LevelHeader header{};
    std::vector<BoxRecord> boxes;
    std::vector<SpawnPointRecord> spawns;
    std::vector<LightRecord> lights;
    std::vector<MeshRecord> meshes;
};

// Reads/writes the binary level format and populates the ECS + physics world.
//
// The format is little-endian (see LevelFormat.h). All I/O is plain byte copies
// of the fixed-width POD records, so reads/writes are valid only between
// little-endian hosts.
namespace LevelLoader {

// Read a .dslv file into `out`. Returns false if the file is missing, too short,
// or has a bad magic / unsupported version. On failure `out` is unspecified.
bool read(const std::filesystem::path& path, LevelData& out);

// Write `data` to a .dslv file (used by the converter tool). The header's counts
// are recomputed from the vector sizes before writing. Returns false on I/O
// failure.
bool write(const std::filesystem::path& path, const LevelData& data);

// Populate the world from already-parsed level data (task 42, extended for
// mesh records):
//   * static boxes -> render meshes (MeshComponent + MaterialComponent) + a
//     physics static body each,
//   * static meshes -> render meshes built from each MeshRecord's baked
//     vertex/index payload + a physics static-mesh body each (real collision,
//     not a box approximation),
//   * enemy spawn points (flags bit0 == 0) -> SpawnPoint entities,
//   * each LightRecord -> a LightComponent entity (instantiated; the engine's
//     updateLights gathers them into the per-frame light buffer),
//   * the player-start spawn (the FIRST spawn whose flags bit0 is set) is NOT
//     made an enemy SpawnPoint; instead, when `playerStart` is non-null its
//     position is written there so the caller can place the player. If the
//     level has no player-start spawn, *playerStart is left untouched.
// No file I/O — pure ECS/physics population, so this is the shared seam both
// load() (the .dslv path) and Lua-driven procedural generation (LevelGen.h)
// populate the world through, instead of duplicating this logic a third time.
void populate(const LevelData& data, entt::registry& world, PhysicsWorld& physics, rhi::IRHIDevice& device,
              rhi::RHITexture albedo, rhi::RHISampler sampler, glm::vec3* playerStart = nullptr);

// Load a level file and populate the world: read(path, data) then
// populate(data, ...). Returns false if the file could not be read, leaving
// the world untouched.
bool load(const std::filesystem::path& path, entt::registry& world, PhysicsWorld& physics, rhi::IRHIDevice& device,
          rhi::RHITexture albedo, rhi::RHISampler sampler, glm::vec3* playerStart = nullptr);

} // namespace LevelLoader

} // namespace ds
