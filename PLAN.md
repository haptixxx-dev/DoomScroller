# DoomScroller - Architecture Plan

## Game
Fast-paced 3D FPS. Doom / ULTRAKILL / HyperDemon style. Arena combat, aggressive movement.

## Target Hardware
| Spec | Minimum | Enhanced |
|------|---------|----------|
| GPU | GTX 1060 3GB | RTX 20xx / RDNA2 / Arc |
| CPU | Intel i5 10th gen | Intel i5 13th gen |
| RAM | 16 GB | 32 GB |
| VRAM budget | < 2 GB assets | 6 GB |

---

## Decided

### Platform & Build
- **Targets**: Windows, macOS, Linux
- **Language**: C++23
- **Build**: CMake + Ninja, presets (debug / release / profile)
- **Dependencies**: git submodules (version-pinned, no registry)
- **Windowing / Input / Audio**: SDL3 (submodule @ release-3.4.10)
- **Dev flag** (`DS_DEV`): hot-reload paths, source-tree assets, extra logging
- **Profile flag** (`DS_PROFILE`): Tracy frame profiler, near-zero overhead when disconnected

### CI / Quality
- **GitHub Actions**: matrix build - Windows (MSVC + Ninja), macOS (Metal), Linux (Vulkan)
- **clang-format**: enforced in CI (`.clang-format` at repo root)
- **clang-tidy**: runs on engine headers in CI (`.clang-tidy` at repo root)
- **Tests**: Catch2 v3.8.0, `tests/ds_tests`, links `engine_headers` only (avoids SDL3/mimalloc global state)

### Rendering Architecture
- **Abstraction**: Custom RHI interface (`IRHIDevice`, `IRHICommandList`, etc.)
  - Engine code talks only to RHI interface, never raw SDL3 GPU / Vulkan / Metal
  - SDL3 GPU backend ships first (`SDL3Device`, `SDL3CommandList`)
  - Vulkan / Metal backends added later without touching engine code
  - Native handle escape hatch (`nativeDevice()`) unblocks mesh shaders when needed
- **Render path (base)**: Forward rendering - lower VRAM pressure, sufficient for doom-style light counts
- **Render path (enhanced)**: Mesh shader pipeline on capable hardware (Vulkan `VK_EXT_mesh_shader` / DX12 / Metal mesh shaders)
- **Tier selection**: runtime capability query at startup via `RHICaps`

### Shader Pipeline
- **Language**: Slang (write-once, runs on SPIRV / MSL / DXIL / GLSL / WGSL / CUDA)
- **Compiler**: Slang 2026.12 prebuilt binary, downloaded via CMake FetchContent
- **Compile step**: `ds_compile_slang()` CMake function, runs `slangc` at build time per platform
  - macOS: SPIRV + MSL
  - Linux: SPIRV
  - Windows: SPIRV + DXIL
- **Runtime**: `ShaderLoader` detects `SDL_GetGPUShaderFormats()` and loads correct bytecode
- **Entry point naming**: `vertMain` / `fragMain` / `compMain`
- **Dev mode**: shader sources in `shaders/`, compiled output in `build/<preset>/shaders/`
- **Hot-reload**: future - filesystem watcher on `.slang` sources, recompile + swap via `ds::paths::shaderSources()`

### Submodules
| Lib | Version | Purpose |
|-----|---------|---------|
| SDL3 | release-3.4.10 | Window, input, audio, GPU backend |
| GLM | 1.0.3 | Math (vectors, matrices, quaternions) |
| STB | HEAD | Image loading (stb_image) |
| cgltf | v1.15 | glTF 2.0 model loading |
| EnTT | v3.16.0 | ECS |
| JoltPhysics | v5.5.0 | Physics |
| miniaudio | 0.11.25 | 3D audio |
| mimalloc | v3.3.2 | General allocator (overrides operator new/delete globally) |
| Lua | v5.4.7 | Scripting / modding |
| Tracy | v0.13.1 | Frame profiler (embedded via TracyClient.cpp, not add_subdirectory) |
| Catch2 | v3.8.0 | Unit tests |

### Entity System
- **EnTT** - ECS, cache-friendly, scales to 10k+ entities

### Physics
- **Jolt** - MIT, multithreaded, scales to complex simulation

### Audio
- **miniaudio** - 3D spatialization, effects, zero deps
- Future: may migrate to FMOD; keep calls behind internal audio API when scope allows

### Memory
- **Frame arena** - per-frame scratch, O(1) bump alloc, free-all at frame end
- **Pool allocators** - fixed-size blocks for entities and components
- **mimalloc** - base general allocator, replaces new/delete everywhere else
- Arenas and pools sit on top of mimalloc for hot paths

### Level Format
- **Custom binary** - fast load, engine-tailored runtime format
- **External converter tool** (separate project) - glTF / other formats to custom binary, enables modding

### Scripting
- **Lua 5.4** - game logic, modding, internal dev automation
- Binding layer required between C++ engine and Lua VM

### Asset Pipeline
- **Shipping**: offline cook step - source assets -> cook tool -> engine binary format (BC7 textures, packed geometry)
- **Dev mode** (`DS_DEV`): `ds::paths::assets()` resolves to source tree, `ds::paths::shaders()` resolves to build output dir

### Networking
- Out of scope - singleplayer first

---

## Current State

### Done
- [x] Repo init, all submodules pinned and building
- [x] CMake build working (clean, all 3 platforms)
- [x] SDL3 GPU window + clear color render loop
- [x] RHI interface (`RHITypes.h`, `IRHIDevice.h`, `IRHICommandList.h`)
- [x] SDL3 GPU backend (`SDL3Device`, `SDL3CommandList`)
- [x] Tracy profiler (`DS_PROFILE` opt-in, `DS_ZONE()`, `DS_FRAME_MARK()`, etc.)
- [x] `DS_DEV` compile flag + `BuildConfig.h` code generation
- [x] Asset path resolver (`ds::paths::init/assets/shaders/shaderSources/binDir`)
- [x] mimalloc global allocator (auto-wired via mimalloc-static)
- [x] Slang 2026.12 prebuilt download + `ds_compile_slang()` CMake function
- [x] `triangle.slang` - first shader, VS + PS, vertex-ID positions, RGB colors
- [x] `ShaderLoader` - runtime format detection, MSL null-termination
- [x] First triangle on screen (no vertex buffer, positions in shader)
- [x] `engine_headers` INTERFACE target for tests without SDL3/mimalloc deps
- [x] GitHub Actions CI - Windows + macOS + Linux build matrix
- [x] clang-format + clang-tidy enforced in CI
- [x] Catch2 v3.8.0 unit tests (22 assertions, 4 test cases)
- [x] Vertex buffer + indexed draw (`Vertex{pos,color}`, cube 8v/36i, `uploadImmediate`)
- [x] GLM wired + `column_major` MVP push constants (SPIRV set=1 binding=0, MSL `[[buffer(1)]]`)
- [x] FPS camera (`Camera`, `lookAt`+`perspective`, WASD+mouse look, dt-scaled)
- [x] Depth buffer (D32Float, `clearDepth=1.0`, `CompareOp::Less`, fixed missing compare op)

### Next (in order)

| # | Task | Depends on | Key deliverable |
|---|------|-----------|-----------------|
| 1 | **Texture loading** | - | stb_image, GPU upload, sampler, UV in `Vertex` + shader |
| 2 | **Mesh loading (cgltf)** | 1 | Load `.glb`, generalize geometry pipeline beyond cube |
| 3 | **ECS (EnTT)** | - | `Transform`, `Mesh`, `Material` components; EnTT world in `Engine` |
| 4 | **Transform system** | 3 | Position/rotation/scale per entity, model matrix, MVP = proj*view*model |
| 5 | **Arena level geometry** | 2, 4 | Simple box room from glTF or procedural; first walkable space |
| 6 | **Basic lighting** | 5 | Normal in `Vertex`, ambient+directional in fragment shader |
| 7 | **Jolt physics** | 4 | Rigid body world, player capsule, collision vs. level, gravity |
| 8 | **Player controller** | 7 | Ground-based movement (jump, sprint); replaces fly camera |
| 9 | **Enemy entity** | 8 | Spawn point, idle/chase/attack state machine, placeholder mesh |
| 10 | **Weapon + hitscan** | 9 | Ray cast via Jolt, damage component, enemy death; closes game loop |

#### Task 1 - Texture loading
- Add `glm::vec2 uv` to `Vertex` struct
- `stb_image` decode + `uploadImmediateTexture` to `RHITexture`
- `createSampler` (linear, repeat); `TextureManager` or simple cache by path
- Update `triangle.slang` (or new `mesh.slang`): `Texture2D` + `SamplerState`, sample in frag
- Update `ShaderLoader`/pipeline: `numSamplers=1` for fragment shader
- Wire `bindFragmentTexture` in render loop

#### Task 2 - Mesh loading (cgltf)
- `Mesh` struct: `RHIBuffer vertexBuffer, indexBuffer; uint32_t indexCount; IndexType indexType`
- `MeshLoader::load(path)` via `cgltf`: parse positions, normals, UVs, indices
- Support multiple primitives per glTF mesh
- Generalize `Engine::render` to draw from `Mesh` rather than hardcoded cube buffers

#### Task 3 - ECS (EnTT)
- Add `entt::registry m_world` to `Engine`
- Components: `Transform`, `MeshComponent`, `MaterialComponent`
- System-style free functions: `renderSystem(registry, cmd, camera)`
- Keep engine-owned resources (device, window) separate from ECS

#### Task 4 - Transform system
- `Transform`: `glm::vec3 position`, `glm::quat rotation`, `glm::vec3 scale`
- `modelMatrix()` computed from TRS decomposition
- MVP push = `proj * view * entity.transform.modelMatrix()`
- Optional: dirty flag + cached matrix

#### Task 5 - Arena level geometry
- Hand-authored box room as glTF (or procedural via code)
- Floor/walls/ceiling as static mesh entities
- No collision yet - visual walkthrough only

#### Task 6 - Basic lighting
- Add `glm::vec3 normal` to `Vertex`; update vertex attributes
- New `mesh.slang` (or extend triangle.slang): Blinn-Phong or Lambertian diffuse
- `LightConstants` push: `vec3 lightDir`, `vec3 lightColor`, `vec3 ambientColor`
- Phong requires normals in world space: push `normalMatrix = transpose(inverse(model))`

#### Task 7 - Jolt physics
- `PhysicsWorld` wrapper: `JPH::PhysicsSystem`, `JPH::BodyInterface`
- Static mesh body for level (triangle mesh shape)
- Player capsule: `JPH::CharacterVirtual` or kinematic capsule
- `PhysicsWorld::step(dt)` called from `Engine::update`

#### Task 8 - Player controller
- `PlayerController`: wraps Jolt capsule, exposes `move(dir, dt)`, `jump()`
- Replace fly-camera WASD with ground-projected movement
- Preserve mouse look in `Camera`; camera follows capsule position + eye offset
- Ground check, coyote time, variable jump

#### Task 9 - Enemy entity
- `EnemyComponent`: health, state (idle/chase/attack), target entity
- `EnemySystem::update(registry, dt, playerPos)`
- Spawn from a `SpawnPoint` component (position in level)
- Placeholder mesh (capsule or cube); Jolt dynamic body

#### Task 10 - Weapon + hitscan
- `WeaponComponent` on player entity: fire rate, damage, ammo
- Left-click fires: `JPH::RayCastResult` from camera origin along front vector
- Hit enemy: subtract health via `HealthComponent`; enemy death removes entity
- Visual feedback: muzzle flash (point light one frame), hit decal (future)
