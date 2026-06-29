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

### Next (in order)
- [ ] Vertex buffer + indexed draw (real geometry, not shader-hardcoded)
- [ ] GLM wired up + push constants for MVP matrix
- [ ] FPS camera (view/projection, mouse look, WASD)
- [ ] Depth buffer (required before overlapping 3D geometry)
- [ ] Texture loading (stb_image + GPU upload + sampler)
- [ ] First arena blockout (simple box rooms)
- [ ] ECS integration (EnTT) for game objects
- [ ] Jolt physics integration
- [ ] Lua scripting bootstrap
