// level_convert — offline converter that emits a binary .dslv level file.
//
// Usage:
//   level_convert <output.dslv> [input.txt] [--gltf input.gltf]
//
// With an input text file, the file is parsed (engine/LevelTextParser.h) into a
// LevelData and written out as binary; on a parse error the message is printed
// and the tool exits non-zero. With no input file (and no --gltf) it falls back
// to emitting the hardcoded DoomScroller arena (the same room Engine::buildArena
// generates plus the enemy spawn corners and a placeholder light) for back-compat.
//
// --gltf bakes real mesh geometry (not box approximations) from a glTF/.glb
// file into MeshRecords: one per triangle primitive, in node-graph order, each
// placed by its owning node's world transform (engine/GltfExtract.h). Uniform
// node scale is baked into the vertices; non-uniform scale/shear is rejected
// with an error naming the offending node. --gltf can be combined with a text
// input file in the same invocation: hand-placed boxes/spawns/lights from the
// .txt plus baked mesh geometry from the glTF in one output file.
//
// The on-disk format (engine/LevelFormat.h) is identical either way.

#include "engine/GltfExtract.h"
#include "engine/LevelFormat.h"
#include "engine/LevelLoader.h"
#include "engine/LevelTextParser.h"

#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace ds;

namespace {

BoxRecord makeBox(float cx, float cy, float cz, float hx, float hy, float hz) {
    BoxRecord b{};
    b.center[0]      = cx;
    b.center[1]      = cy;
    b.center[2]      = cz;
    b.halfExtents[0] = hx;
    b.halfExtents[1] = hy;
    b.halfExtents[2] = hz;
    b.color[0]       = 1.f;
    b.color[1]       = 1.f;
    b.color[2]       = 1.f;
    b.materialRef    = 0;
    return b;
}

SpawnPointRecord makeSpawn(float x, float y, float z, uint32_t flags) {
    SpawnPointRecord s{};
    s.position[0] = x;
    s.position[1] = y;
    s.position[2] = z;
    s.flags       = flags;
    return s;
}

// Build the in-memory arena: six slabs (floor/ceiling/4 walls), matching the
// collision boxes from Engine::buildArena, plus spawn points and one light.
LevelData buildArenaLevel() {
    constexpr float W = 10.f, H = 5.f, D = 10.f, T = 0.1f;

    LevelData data;
    data.boxes.push_back(makeBox(0.f, -T, 0.f, W, T, D));           // floor
    data.boxes.push_back(makeBox(0.f, H + T, 0.f, W, T, D));        // ceiling
    data.boxes.push_back(makeBox(0.f, H / 2, -D - T, W, H / 2, T)); // north wall
    data.boxes.push_back(makeBox(0.f, H / 2, D + T, W, H / 2, T));  // south wall
    data.boxes.push_back(makeBox(-W - T, H / 2, 0.f, T, H / 2, D)); // west wall
    data.boxes.push_back(makeBox(W + T, H / 2, 0.f, T, H / 2, D));  // east wall

    data.spawns.push_back(makeSpawn(0.f, 1.7f, 0.f, 1u)); // player start
    data.spawns.push_back(makeSpawn(-7.f, 1.5f, -7.f, 0u));
    data.spawns.push_back(makeSpawn(7.f, 1.5f, -7.f, 0u));
    data.spawns.push_back(makeSpawn(0.f, 1.5f, 7.f, 0u));

    LightRecord l{};
    l.position[0] = 0.f;
    l.position[1] = H - 0.5f;
    l.position[2] = 0.f;
    l.color[0]    = 1.f;
    l.color[1]    = 1.f;
    l.color[2]    = 1.f;
    l.radius      = 20.f;
    l.intensity   = 1.f;
    data.lights.push_back(l);

    return data;
}

// Read an entire file into a string. Returns false (and leaves `out` unspecified)
// if the file cannot be opened or read.
bool readFile(const char* path, std::string& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    out.clear();
    char buf[4096];
    size_t got = 0;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, got);
    }
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

// Bakes every triangle primitive in `path` into one MeshRecord each (real
// mesh geometry, not box approximations), appending to `data.meshes`. Each
// primitive's owning node's world position/rotation places it; uniform scale
// is already baked into the vertices by extractNodePrimitives. Prints an
// error and returns false on parse failure or non-uniform node scale.
bool appendGltfMeshes(const char* path, LevelData& data, std::string& error) {
    std::vector<gltf::ExtractedNodePrimitive> nodePrims;
    try {
        nodePrims = gltf::extractNodePrimitives(path);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    for (const auto& np : nodePrims) {
        MeshRecord mr{};
        mr.header.position[0] = np.worldPosition.x;
        mr.header.position[1] = np.worldPosition.y;
        mr.header.position[2] = np.worldPosition.z;
        mr.header.rotation[0] = np.worldRotation.x;
        mr.header.rotation[1] = np.worldRotation.y;
        mr.header.rotation[2] = np.worldRotation.z;
        mr.header.rotation[3] = np.worldRotation.w;
        mr.vertices            = np.primitive.vertices;
        mr.indices             = np.primitive.indices;
        mr.header.vertexCount  = static_cast<uint32_t>(mr.vertices.size());
        mr.header.indexCount   = static_cast<uint32_t>(mr.indices.size());
        data.meshes.push_back(std::move(mr));
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <output.dslv> [input.txt] [--gltf input.gltf]\n", argv[0]);
        return 2;
    }

    const char* outputPath = argv[1];
    const char* textPath   = nullptr;
    const char* gltfPath   = nullptr;

    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--gltf") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "level_convert: --gltf requires a path argument\n");
                return 2;
            }
            gltfPath = argv[++i];
        } else if (textPath == nullptr) {
            textPath = argv[i];
        } else {
            std::fprintf(stderr, "usage: %s <output.dslv> [input.txt] [--gltf input.gltf]\n", argv[0]);
            return 2;
        }
    }

    LevelData data;
    if (textPath != nullptr) {
        std::string text;
        if (!readFile(textPath, text)) {
            std::fprintf(stderr, "level_convert: failed to read '%s'\n", textPath);
            return 1;
        }
        std::string error;
        std::optional<LevelData> parsed = parseLevelText(text, &error);
        if (!parsed.has_value()) {
            std::fprintf(stderr, "level_convert: parse error in '%s': %s\n", textPath, error.c_str());
            return 1;
        }
        data = std::move(*parsed);
    } else if (gltfPath == nullptr) {
        data = buildArenaLevel();
    }

    if (gltfPath != nullptr) {
        std::string error;
        if (!appendGltfMeshes(gltfPath, data, error)) {
            std::fprintf(stderr, "level_convert: failed to convert '%s': %s\n", gltfPath, error.c_str());
            return 1;
        }
    }

    if (!LevelLoader::write(outputPath, data)) {
        std::fprintf(stderr, "level_convert: failed to write '%s'\n", outputPath);
        return 1;
    }

    std::printf("level_convert: wrote %s (%zu boxes, %zu spawns, %zu lights, %zu meshes)\n", outputPath,
                data.boxes.size(), data.spawns.size(), data.lights.size(), data.meshes.size());
    return 0;
}
