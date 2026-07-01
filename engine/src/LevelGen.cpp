#include "engine/LevelGen.h"

#include "engine/ScriptSystem.h"

namespace ds {

bool generateLevelFromLua(const std::filesystem::path& scriptPath, LevelData& outData,
                          std::vector<LuaMeshPlacement>& outMeshes) {
    outData = LevelData{};
    outMeshes.clear();

    ScriptSystem::Callbacks cb{};
    cb.levelAddBox = [&outData](glm::vec3 center, glm::vec3 halfExtents, glm::vec3 color) {
        BoxRecord b{};
        b.center[0]      = center.x;
        b.center[1]      = center.y;
        b.center[2]      = center.z;
        b.halfExtents[0] = halfExtents.x;
        b.halfExtents[1] = halfExtents.y;
        b.halfExtents[2] = halfExtents.z;
        b.color[0]       = color.x;
        b.color[1]       = color.y;
        b.color[2]       = color.z;
        outData.boxes.push_back(b);
    };
    cb.levelAddMesh = [&outMeshes](const std::string& path, glm::vec3 position, glm::quat rotation) {
        outMeshes.push_back(LuaMeshPlacement{path, position, rotation});
    };
    cb.levelAddSpawn = [&outData](glm::vec3 position, bool isPlayerStart, int archetypeHint) {
        SpawnPointRecord s{};
        s.position[0]          = position.x;
        s.position[1]          = position.y;
        s.position[2]          = position.z;
        uint32_t archetypeBits = archetypeHint >= 0 ? (static_cast<uint32_t>(archetypeHint) + 1u) & 0x3u : 0u;
        s.flags                = (isPlayerStart ? 1u : 0u) | (archetypeBits << 1);
        outData.spawns.push_back(s);
    };
    cb.levelAddLight = [&outData](glm::vec3 position, glm::vec3 color, float radius, float intensity) {
        LightRecord l{};
        l.position[0] = position.x;
        l.position[1] = position.y;
        l.position[2] = position.z;
        l.color[0]    = color.x;
        l.color[1]    = color.y;
        l.color[2]    = color.z;
        l.radius      = radius;
        l.intensity   = intensity;
        outData.lights.push_back(l);
    };

    ScriptSystem gen;
    if (!gen.init(cb))
        return false;
    if (!gen.loadFile(scriptPath.string()))
        return false;

    return !outData.boxes.empty() || !outData.spawns.empty() || !outData.lights.empty() || !outMeshes.empty();
}

} // namespace ds
