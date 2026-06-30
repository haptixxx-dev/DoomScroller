#include "engine/LevelLoader.h"

#include "engine/PhysicsWorld.h"
#include "engine/Vertex.h"
#include "engine/ecs/Components.h"
#include "engine/rhi/IRHIDevice.h"

#include <cstdint>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <utility>

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
        out.meshes.clear();

        if (header.boxCount > 0 &&
            std::fread(out.boxes.data(), sizeof(BoxRecord), header.boxCount, f) != header.boxCount)
            break;
        if (header.spawnCount > 0 &&
            std::fread(out.spawns.data(), sizeof(SpawnPointRecord), header.spawnCount, f) != header.spawnCount)
            break;
        if (header.lightCount > 0 &&
            std::fread(out.lights.data(), sizeof(LightRecord), header.lightCount, f) != header.lightCount)
            break;

        bool meshReadFailed = false;
        out.meshes.reserve(header.meshCount);
        for (uint32_t i = 0; i < header.meshCount; ++i) {
            MeshRecord mr{};
            if (std::fread(&mr.header, sizeof(MeshRecordHeader), 1, f) != 1) {
                meshReadFailed = true;
                break;
            }
            mr.vertices.assign(mr.header.vertexCount, Vertex{});
            if (mr.header.vertexCount > 0 &&
                std::fread(mr.vertices.data(), sizeof(Vertex), mr.header.vertexCount, f) != mr.header.vertexCount) {
                meshReadFailed = true;
                break;
            }
            mr.indices.assign(mr.header.indexCount, 0u);
            if (mr.header.indexCount > 0 &&
                std::fread(mr.indices.data(), sizeof(uint32_t), mr.header.indexCount, f) != mr.header.indexCount) {
                meshReadFailed = true;
                break;
            }
            out.meshes.push_back(std::move(mr));
        }
        if (meshReadFailed)
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
    header.meshCount   = static_cast<uint32_t>(data.meshes.size());

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

        bool meshWriteFailed = false;
        for (const auto& mr : data.meshes) {
            MeshRecordHeader mh = mr.header;
            mh.vertexCount      = static_cast<uint32_t>(mr.vertices.size());
            mh.indexCount       = static_cast<uint32_t>(mr.indices.size());
            if (std::fwrite(&mh, sizeof(MeshRecordHeader), 1, f) != 1) {
                meshWriteFailed = true;
                break;
            }
            if (mh.vertexCount > 0 &&
                std::fwrite(mr.vertices.data(), sizeof(Vertex), mh.vertexCount, f) != mh.vertexCount) {
                meshWriteFailed = true;
                break;
            }
            if (mh.indexCount > 0 &&
                std::fwrite(mr.indices.data(), sizeof(uint32_t), mh.indexCount, f) != mh.indexCount) {
                meshWriteFailed = true;
                break;
            }
        }
        if (meshWriteFailed)
            break;

        ok = true;
    } while (false);

    std::fclose(f);
    return ok;
}

void LevelLoader::populate(const LevelData& data, entt::registry& world, PhysicsWorld& physics,
                           rhi::IRHIDevice& device, rhi::RHITexture albedo, rhi::RHISampler sampler,
                           glm::vec3* playerStart) {
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

    bool playerStartSeen = false;
    for (const auto& sp : data.spawns) {
        glm::vec3 pos{sp.position[0], sp.position[1], sp.position[2]};
        // flags bit0 marks the player start (LevelFormat.h). The FIRST such spawn
        // positions the player and is NOT registered as an enemy SpawnPoint;
        // every other spawn becomes an enemy SpawnPoint for wave placement.
        if ((sp.flags & 0x1u) != 0u && !playerStartSeen) {
            playerStartSeen = true;
            if (playerStart != nullptr)
                *playerStart = pos;
            continue;
        }
        auto e = world.create();
        world.emplace<SpawnPoint>(e, SpawnPoint{pos});
    }

    // Light records (task 42): one LightComponent entity per record. No
    // Transform is attached, so updateLights() uses the component's own
    // position (it follows a Transform only when present).
    for (const auto& lr : data.lights) {
        auto e = world.create();
        LightComponent lc{};
        lc.position  = glm::vec3{lr.position[0], lr.position[1], lr.position[2]};
        lc.color     = glm::vec3{lr.color[0], lr.color[1], lr.color[2]};
        lc.radius    = lr.radius;
        lc.intensity = lr.intensity;
        world.emplace<LightComponent>(e, lc);
    }

    // Static mesh records (real, non-box collision geometry, see
    // LevelFormat.h's MeshRecordHeader doc comment). Always uploaded as
    // Uint32 index buffers: simpler than the runtime loader's 16/32-bit
    // narrowing, acceptable for infrequent, large, one-off level geometry.
    for (const auto& mr : data.meshes) {
        glm::vec3 pos{mr.header.position[0], mr.header.position[1], mr.header.position[2]};
        // glm::quat's constructor takes (w, x, y, z); the on-disk layout is
        // x,y,z,w (LevelFormat.h), so the components must be reordered here.
        glm::quat rot{mr.header.rotation[3], mr.header.rotation[0], mr.header.rotation[1], mr.header.rotation[2]};

        rhi::BufferDesc vbd{};
        vbd.size  = mr.vertices.size() * sizeof(Vertex);
        vbd.usage = rhi::BufferUsage::Vertex;
        auto vb   = device.createBuffer(vbd);
        device.uploadImmediate(vb, mr.vertices.data(), vbd.size);

        rhi::BufferDesc ibd{};
        ibd.size  = mr.indices.size() * sizeof(uint32_t);
        ibd.usage = rhi::BufferUsage::Index;
        auto ib   = device.createBuffer(ibd);
        device.uploadImmediate(ib, mr.indices.data(), ibd.size);

        auto e = world.create();
        Transform t{};
        t.position = pos;
        t.rotation = rot;
        world.emplace<Transform>(e, t);
        world.emplace<MeshComponent>(
            e, MeshComponent{vb, ib, static_cast<uint32_t>(mr.indices.size()), rhi::IndexType::Uint32});
        world.emplace<MaterialComponent>(e, MaterialComponent{albedo, sampler});

        std::vector<glm::vec3> physicsVerts(mr.vertices.size());
        for (size_t i = 0; i < mr.vertices.size(); ++i)
            physicsVerts[i] = mr.vertices[i].pos;
        physics.addStaticMesh(physicsVerts, mr.indices, pos, rot);
    }
}

bool LevelLoader::load(const std::filesystem::path& path, entt::registry& world, PhysicsWorld& physics,
                       rhi::IRHIDevice& device, rhi::RHITexture albedo, rhi::RHISampler sampler,
                       glm::vec3* playerStart) {
    LevelData data;
    if (!read(path, data))
        return false;
    populate(data, world, physics, device, albedo, sampler, playerStart);
    return true;
}

} // namespace ds
