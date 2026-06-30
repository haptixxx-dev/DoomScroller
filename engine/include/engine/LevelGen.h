#pragma once

#include "engine/LevelLoader.h"

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace ds {

// A Lua-requested mesh placement (ds.level.add_mesh): a real asset on disk to
// load via MeshLoader at engine startup, placed by position/rotation. Kept
// separate from LevelData::meshes (which holds geometry already BAKED from a
// glTF source by tools/level_convert) — Lua-generated levels load meshes
// fresh every run via the live rhi::IRHIDevice, they are never serialized.
struct LuaMeshPlacement {
    std::string path;
    glm::vec3 position{0.f};
    glm::quat rotation{1.f, 0.f, 0.f, 0.f};
};

// Runs `scriptPath` through a throwaway ScriptSystem instance wired only with
// the ds.level.* callbacks (NOT Engine's main gameplay m_scripts — that one's
// camera/player callbacks reference engine state that doesn't exist yet this
// early in initScene()), accumulating every add_box/add_spawn/add_light call
// into `outData` and every add_mesh call into `outMeshes`.
//
// Returns false (leaving outData/outMeshes unspecified) if the script is
// missing, fails to run, or made zero ds.level.add_* calls — callers should
// treat that as "no level generated" and fall through to the next source
// (.dslv, then the hardcoded buildArena()), matching the engine's existing
// graceful-degradation philosophy for level loading.
bool generateLevelFromLua(const std::filesystem::path& scriptPath, LevelData& outData,
                          std::vector<LuaMeshPlacement>& outMeshes);

} // namespace ds
