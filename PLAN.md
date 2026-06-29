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
- **Build**: CMake + Ninja, presets (debug / release)
- **Dependencies**: git submodules (version-pinned, no registry)
- **Windowing / Input / Audio**: SDL3 (submodule @ release-3.4.10)

### Rendering Architecture
- **Abstraction**: Custom RHI interface (`IRHIDevice`, `IRHICommandList`, etc.)
  - Engine code talks only to RHI interface, never raw SDL3 GPU / Vulkan / Metal
  - SDL3 GPU backend ships first (`SDL3Device`, `SDL3CommandList`)
  - Vulkan / Metal backends added later without touching engine code
  - Native handle escape hatch (`nativeDevice()`) unblocks mesh shaders when needed
- **Render path (base)**: Forward rendering - lower VRAM pressure, sufficient for doom-style light counts
- **Render path (enhanced)**: Mesh shader pipeline on capable hardware (Vulkan `VK_EXT_mesh_shader` / DX12 / Metal mesh shaders)
- **Tier selection**: runtime capability query at startup via `RHICaps`

### Submodules
| Lib | Version | Purpose |
|-----|---------|---------|
| SDL3 | release-3.4.10 | Window, input, audio, GPU backend |
| GLM | 1.0.3 | Math (vectors, matrices, quaternions) |
| STB | HEAD | Image loading |
| cgltf | v1.15 | glTF 2.0 model loading |
| EnTT | v3.16.0 | ECS |
| JoltPhysics | v5.5.0 | Physics |
| miniaudio | 0.11.25 | 3D audio |
| mimalloc | v3.3.2 | General allocator |
| Lua | v5.4.7 | Scripting / modding |

### Shader Pipeline
- **Language**: HLSL
- **Cross-compilation**: SDL_shadercross (HLSL -> SPIRV / MSL / DXIL)
- **Shipping**: pre-compiled bytecode, no compiler linked
- **Dev mode** (`DS_DEV`): filesystem watcher on `shaders/*.hlsl`, recompile + hot-swap on change

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
- **Dev mode** (`DS_DEV`): runtime load from source (glTF, PNG) for fast iteration

### Networking
- Out of scope - singleplayer first

---

## Current State
- [x] Repo init, all submodules pinned and building
- [x] CMake build working (457 targets, clean)
- [x] SDL3 GPU window + clear color render loop
- [x] RHI interface designed (`RHITypes.h`, `IRHIDevice.h`, `IRHICommandList.h`)
- [x] SDL3 GPU backend implemented (`SDL3Device`, `SDL3CommandList`)
- [x] Engine owns `IRHIDevice` - no raw SDL3 GPU calls outside backend
- [ ] First triangle on screen
- [ ] FPS camera
- [ ] Textured mesh
- [ ] First arena blockout
