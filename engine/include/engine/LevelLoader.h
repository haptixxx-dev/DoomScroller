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

// LevelData is the in-memory form of a parsed .dslv file: the header plus the
// three record blocks. Used by both the runtime loader and the offline
// converter tool (LevelLoader::write / LevelLoader::read).
struct LevelData {
    LevelHeader header{};
    std::vector<BoxRecord> boxes;
    std::vector<SpawnPointRecord> spawns;
    std::vector<LightRecord> lights;
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

// Load a level file and populate the world (task 42):
//   * static boxes -> render meshes (MeshComponent + MaterialComponent) + a
//     physics static body each,
//   * enemy spawn points (flags bit0 == 0) -> SpawnPoint entities,
//   * each LightRecord -> a LightComponent entity (instantiated; the engine's
//     updateLights gathers them into the per-frame light buffer),
//   * the player-start spawn (the FIRST spawn whose flags bit0 is set) is NOT
//     made an enemy SpawnPoint; instead, when `playerStart` is non-null its
//     position is written there so the caller can place the player. If the
//     level has no player-start spawn, *playerStart is left untouched.
// Returns false if the file could not be read, leaving the world untouched.
bool load(const std::filesystem::path& path, entt::registry& world, PhysicsWorld& physics, rhi::IRHIDevice& device,
          rhi::RHITexture albedo, rhi::RHISampler sampler, glm::vec3* playerStart = nullptr);

} // namespace LevelLoader

} // namespace ds
