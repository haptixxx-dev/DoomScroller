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
  - Engine code talks only to RHI interface
  - SDL3 GPU backend ships first
  - Vulkan / Metal backends added later without touching engine code
- **Render path (base)**: Forward rendering - lower VRAM pressure, sufficient for doom-style light counts
- **Render path (enhanced)**: Mesh shader pipeline on capable hardware (Vulkan `VK_EXT_mesh_shader` / DX12 / Metal mesh shaders)
- **Tier selection**: runtime capability query at startup

### Libraries (pending submodule addition)
| Lib | Purpose | Format |
|-----|---------|--------|
| GLM | Math (vectors, matrices, quaternions) | header-only submodule |
| STB | Image loading | header-only submodule |
| cgltf | glTF 2.0 model loading | header-only submodule |
| entt | ECS | header-only submodule |
| Jolt | Physics | submodule |
| miniaudio | 3D audio | header-only submodule |
| mimalloc | General allocator | submodule |
| Lua 5.4 | Scripting / modding | submodule |

### Shader Pipeline (tentative)
- **Language**: HLSL
- **Cross-compilation**: SDL_shadercross (HLSL → SPIRV / MSL / DXIL)
- Offline compilation preferred; runtime fallback for dev

---

## Decided (continued)

### Entity System
- **entt** (submodule) - ECS, cache-friendly, scales to 10k+ entities

### Physics
- **Jolt** (submodule) - MIT, multithreaded, modern, capable of scaling to complex simulation

### Audio
- **miniaudio** (submodule, header-only) - 3D spatialization, effects, zero deps
- Future: may migrate to FMOD or expand; keep audio calls behind internal API when scope allows

### Memory
- **Frame arena** - per-frame scratch allocation, O(1) bump, free-all at frame end
- **Pool allocators** - fixed-size blocks for entities, components
- **mimalloc** (submodule) - base general allocator replacing new/delete everywhere else
- No custom allocator replaces mimalloc; arenas/pools sit on top for hot paths

### Level Format
- **Custom binary** - runtime format, fast load, engine-tailored
- **External converter tool** (separate project) - glTF / other → custom binary, enables modding support

### Scripting
- **Lua 5.4** (submodule) - game logic, modding, internal dev automation
- Binding layer needed between C++ engine and Lua VM

### Asset Pipeline
- **Shipping**: offline cook step - source assets → cook tool → engine binary format (BC7 textures, packed geometry)
- **Dev mode** (`DS_DEV`): runtime load from source (glTF, PNG) - fast iteration, no cook step
- External converter tool (separate project) handles glTF → binary; doubles as modding entry point

### Shaders
- **Shipping**: pre-compiled bytecode (SPIRV / MSL / DXIL) - zero startup cost, no compiler linked
- **Dev mode** (`DS_DEV`): filesystem watcher on `shaders/*.hlsl` → recompile via SDL_shadercross → hot-swap pipeline without restart
- SDL_shadercross linked only in dev builds

### Networking
- Out of scope - singleplayer first.

---

## Current State
- [x] Repo init, SDL3 submodule pinned
- [x] CMake build working, binary produces
- [x] Basic SDL3 GPU window + render loop
- [ ] RHI interface design
- [ ] First triangle
