#include "engine/LevelLoader.h"

#include "engine/PhysicsWorld.h"
#include "engine/Vertex.h"
#include "engine/ecs/Components.h"
#include "engine/rhi/IRHIDevice.h"

#include <cstdint>
#include <cstdio>
#include <glm/glm.hpp>

namespace ds {

namespace {

// Build a colored, textured box mesh (24 verts, 36 indices) centered at the
// origin with the given half-extents. Mirrors the makeBoxMesh helper in
// Engine.cpp; kept local so the loader does not depend on Engine internals.
MeshComponent makeLevelBox(rhi::IRHIDevice& device, float hw, float hh, float hd, glm::vec3 color) {
    struct BoxVert {
        glm::vec3 pos;
        glm::vec3 col;
        glm::vec2 uv;
        glm::vec3 normal;
    };

    const BoxVert verts[24] = {
        // +Y top
        {{-hw, hh, -hd}, color, {0, 0}, {0, 1, 0}},
        {{hw, hh, -hd}, color, {1, 0}, {0, 1, 0}},
        {{hw, hh, hd}, color, {1, 1}, {0, 1, 0}},
        {{-hw, hh, hd}, color, {0, 1}, {0, 1, 0}},
        // -Y bottom
        {{-hw, -hh, hd}, color, {0, 0}, {0, -1, 0}},
        {{hw, -hh, hd}, color, {1, 0}, {0, -1, 0}},
        {{hw, -hh, -hd}, color, {1, 1}, {0, -1, 0}},
        {{-hw, -hh, -hd}, color, {0, 1}, {0, -1, 0}},
        // +Z front
        {{-hw, -hh, hd}, color, {0, 0}, {0, 0, 1}},
        {{hw, -hh, hd}, color, {1, 0}, {0, 0, 1}},
        {{hw, hh, hd}, color, {1, 1}, {0, 0, 1}},
        {{-hw, hh, hd}, color, {0, 1}, {0, 0, 1}},
        // -Z back
        {{hw, -hh, -hd}, color, {0, 0}, {0, 0, -1}},
        {{-hw, -hh, -hd}, color, {1, 0}, {0, 0, -1}},
        {{-hw, hh, -hd}, color, {1, 1}, {0, 0, -1}},
        {{hw, hh, -hd}, color, {0, 1}, {0, 0, -1}},
        // +X right
        {{hw, -hh, hd}, color, {0, 0}, {1, 0, 0}},
        {{hw, -hh, -hd}, color, {1, 0}, {1, 0, 0}},
        {{hw, hh, -hd}, color, {1, 1}, {1, 0, 0}},
        {{hw, hh, hd}, color, {0, 1}, {1, 0, 0}},
        // -X left
        {{-hw, -hh, -hd}, color, {0, 0}, {-1, 0, 0}},
        {{-hw, -hh, hd}, color, {1, 0}, {-1, 0, 0}},
        {{-hw, hh, hd}, color, {1, 1}, {-1, 0, 0}},
        {{-hw, hh, -hd}, color, {0, 1}, {-1, 0, 0}},
    };
    const uint16_t idx[36] = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
                              12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

    rhi::BufferDesc vbd{};
    vbd.size  = sizeof(verts);
    vbd.usage = rhi::BufferUsage::Vertex;
    auto vb   = device.createBuffer(vbd);
    device.uploadImmediate(vb, verts, vbd.size);

    rhi::BufferDesc ibd{};
    ibd.size  = sizeof(idx);
    ibd.usage = rhi::BufferUsage::Index;
    auto ib   = device.createBuffer(ibd);
    device.uploadImmediate(ib, idx, ibd.size);

    return MeshComponent{vb, ib, 36u, rhi::IndexType::Uint16};
}

} // namespace

bool LevelLoader::read(const std::filesystem::path& path, LevelData& out) {
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f)
        return false;

    bool ok = false;
    do {
        LevelHeader header{};
        if (std::fread(&header, sizeof(header), 1, f) != 1)
            break;
        if (header.magic != kLevelMagic || header.version != kLevelVersion)
            break;

        out.header = header;
        out.boxes.assign(header.boxCount, BoxRecord{});
        out.spawns.assign(header.spawnCount, SpawnPointRecord{});
        out.lights.assign(header.lightCount, LightRecord{});

        if (header.boxCount > 0 &&
            std::fread(out.boxes.data(), sizeof(BoxRecord), header.boxCount, f) != header.boxCount)
            break;
        if (header.spawnCount > 0 &&
            std::fread(out.spawns.data(), sizeof(SpawnPointRecord), header.spawnCount, f) != header.spawnCount)
            break;
        if (header.lightCount > 0 &&
            std::fread(out.lights.data(), sizeof(LightRecord), header.lightCount, f) != header.lightCount)
            break;

        ok = true;
    } while (false);

    std::fclose(f);
    return ok;
}

bool LevelLoader::write(const std::filesystem::path& path, const LevelData& data) {
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f)
        return false;

    LevelHeader header = data.header;
    header.magic       = kLevelMagic;
    header.version     = kLevelVersion;
    header.boxCount    = static_cast<uint32_t>(data.boxes.size());
    header.spawnCount  = static_cast<uint32_t>(data.spawns.size());
    header.lightCount  = static_cast<uint32_t>(data.lights.size());

    bool ok = false;
    do {
        if (std::fwrite(&header, sizeof(header), 1, f) != 1)
            break;
        if (header.boxCount > 0 &&
            std::fwrite(data.boxes.data(), sizeof(BoxRecord), header.boxCount, f) != header.boxCount)
            break;
        if (header.spawnCount > 0 &&
            std::fwrite(data.spawns.data(), sizeof(SpawnPointRecord), header.spawnCount, f) != header.spawnCount)
            break;
        if (header.lightCount > 0 &&
            std::fwrite(data.lights.data(), sizeof(LightRecord), header.lightCount, f) != header.lightCount)
            break;
        ok = true;
    } while (false);

    std::fclose(f);
    return ok;
}

bool LevelLoader::load(const std::filesystem::path& path, entt::registry& world, PhysicsWorld& physics,
                       rhi::IRHIDevice& device, rhi::RHITexture albedo, rhi::RHISampler sampler) {
    LevelData data;
    if (!read(path, data))
        return false;

    for (const auto& box : data.boxes) {
        glm::vec3 center{box.center[0], box.center[1], box.center[2]};
        glm::vec3 half{box.halfExtents[0], box.halfExtents[1], box.halfExtents[2]};
        glm::vec3 color{box.color[0], box.color[1], box.color[2]};

        auto e = world.create();
        Transform t{};
        t.position = center;
        world.emplace<Transform>(e, t);
        world.emplace<MeshComponent>(e, makeLevelBox(device, half.x, half.y, half.z, color));
        world.emplace<MaterialComponent>(e, MaterialComponent{albedo, sampler});

        physics.addStaticBox(center, half);
    }

    for (const auto& sp : data.spawns) {
        auto e = world.create();
        world.emplace<SpawnPoint>(e, SpawnPoint{glm::vec3{sp.position[0], sp.position[1], sp.position[2]}});
    }

    // Light records are reserved for the lighting task and intentionally not
    // instantiated here yet.

    return true;
}

} // namespace ds
