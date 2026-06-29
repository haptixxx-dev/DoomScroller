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
- [x] Texture loading (stb_image, `TextureManager`, sampler, UV in `Vertex`+shader)
- [x] Mesh loading (cgltf, `MeshLoader`, POSITION/NORMAL/TEXCOORD_0)
- [x] ECS (EnTT registry, `Transform`/`Mesh`/`Material`/`Enemy`/`Weapon`/`SpawnPoint`)
- [x] Transform system (TRS `modelMatrix()`, MVP+model push constants)
- [x] Arena geometry (20x5x20 box room, inward normals, tiled checkerboard)
- [x] Basic lighting (vertex normals, Lambertian + ambient, hardcoded sun)
- [x] Jolt physics (`PhysicsWorld` PIMPL, static boxes, capsules, raycasts)
- [x] Player controller (capsule, ground WASD, coyote jump, eye offset)
- [x] Enemy entity (3 spawns, idle/chase/attack FSM, physics-synced, red box)
- [x] Weapon hitscan (`castRay` from camera, damage by bodyId, enemy death)
- [x] Fix: push constants `register(b0)` (SDL3 Metal vert uniform = buffer 0)
- [x] Fix: shutdown malloc crash (force-load mimalloc `__interpose` on macOS)
- [x] `engine_math` test target + `ds_engine_tests` (Transform/Camera/component math)

### Next (in order)

Loop is closed (move, shoot, kill). Phase 2 = make it a *game* that feels good:
two-way combat, feedback (audio/UI/VFX), movement tech, content pipeline.

| # | Task | Depends on | Key deliverable |
|---|------|-----------|-----------------|
| 11 | **Player health + enemy attacks** | - | Enemies deal damage, player health, death + respawn; two-way combat |
| 12 | **Audio (miniaudio)** | - | `AudioSystem`, spatial SFX (shots/hits/death), music bus, volume |
| 13 | **HUD / UI overlay** | 11 | 2D pass: crosshair, health/ammo, enemy count, damage flash |
| 14 | **VFX / particle system** | - | Instanced billboards: muzzle flash, blood, impact sparks, explosions |
| 15 | **Dynamic lights** | 14 | Multi-light forward, point lights, muzzle-flash + explosion light |
| 16 | **Movement tech** | 11 | Dash (i-frames), slide, air control; ULTRAKILL/HyperDemon feel |
| 17 | **Projectile weapons + switching** | 14 | Rocket/plasma (travel + splash), weapon slots, per-weapon stats |
| 18 | **Wave spawner + game state** | 11, 13 | State machine (menu/play/dead), wave system, score, win/lose |
| 19 | **Level format + loader** | - | Custom binary level, converter tool stub, load arena from file |
| 20 | **Lua scripting hooks** | 18 | Bind spawn/wave/event API, data-drive enemy stats + wave config |

#### Task 11 - Player health + enemy attacks
- `HealthComponent` (current/max); attach to player entity
- Enemy `Attack` state: when in `attackRange`, deal damage on cooldown
- Melee (contact) damage first; ranged enemy projectiles deferred to task 17
- Player death: `m_running` stays, transition to dead state (task 18 hooks this)
- Respawn: reset player capsule to spawn, restore health, clear/reset enemies
- I-frame timer field on player for task 16 dash

#### Task 12 - Audio (miniaudio)
- `AudioSystem` wrapper: `ma_engine` init/shutdown, master volume
- One-shot SFX: `play(soundId)`; 3D spatial: `playAt(soundId, pos)` with listener = camera
- Sound assets via `ds::paths::assets()`; cache decoded sounds by path
- Hook events: weapon fire, enemy hit, enemy death, player hit, footstep
- Music bus (looping), separate volume from SFX
- Keep behind internal API (FMOD migration path per PLAN)

#### Task 13 - HUD / UI overlay
- 2D render pass after 3D: orthographic, no depth, alpha blend
- `ui.slang`: textured quad shader (`pos`+`uv`+`color`), screen-space coords
- Crosshair (center), health bar, ammo counter, enemy/wave count
- Bitmap font or stb_truetype for numbers/text
- Damage flash (red vignette quad on player hit), low-health pulse
- `UISystem` immediate-mode helpers: `drawQuad`, `drawText`

#### Task 14 - VFX / particle system
- `ParticleSystem`: pooled CPU particles, GPU instanced billboard draw
- `particle.slang`: instanced quads, camera-facing, per-instance pos/size/color/uv
- Emitters: muzzle flash, blood burst (enemy hit), impact sparks (wall), explosion
- Additive blend for sparks/flash, alpha for blood/smoke
- Lifetime, velocity, gravity, fade; `emit(type, pos, dir, count)`
- Replaces "future" muzzle-flash placeholder from task 10

#### Task 15 - Dynamic lights
- Forward multi-light: `LightComponent` (point: pos, color, radius, intensity)
- Push/UBO array of N lights (cap ~8-16) to `mesh.slang` fragment
- Per-fragment accumulate: attenuation `1/(d^2)` clamped to radius
- Transient lights: muzzle flash (1-2 frames), explosion flash, projectile glow
- Light culling deferred (small counts fine for arena)

#### Task 16 - Movement tech
- Extend `PlayerController`: `dash(dir)` - impulse burst + i-frames + cooldown
- Slide: crouch while sprinting -> lower capsule, preserve momentum, friction curve
- Air control tuning, ground accel/decel curves (ULTRAKILL-style snappy)
- Dash charges (regen over time), dash SFX/VFX hook (tasks 12/14)
- Tie i-frames to `HealthComponent` damage gate from task 11

#### Task 17 - Projectile weapons + switching
- `ProjectileComponent`: velocity, damage, lifetime, splash radius, owner
- Projectile system: integrate motion, Jolt collision query, splash damage falloff
- Weapon types: hitscan (done), rocket (travel+splash), plasma (fast projectile)
- `WeaponComponent` -> weapon slots array; number keys / wheel to switch
- Per-weapon: damage, fire rate, projectile type, ammo, muzzle VFX/SFX
- Splash spawns explosion VFX + light (tasks 14/15)

#### Task 18 - Wave spawner + game state
- `GameState` enum: `Menu`, `Playing`, `Dead`, `Victory`; `Engine` owns current
- Wave system: spawn N enemies per wave from `SpawnPoint`s, escalate count/type
- Wave clear detection -> next wave (delay/intermission)
- Score: kills, time, combo; persist high score to file
- Menu + game-over screens via UI (task 13); restart resets world
- Player death (task 11) -> `Dead` state

#### Task 19 - Level format + loader
- Custom binary level format: header, static geometry, spawn points, light defs
- `LevelLoader::load(path)` -> populate ECS (static meshes, spawns, lights)
- Converter tool stub (`tools/`): glTF -> custom binary (offline cook)
- Replace hardcoded `buildArena` with loaded level
- Versioned format; embed material/texture refs by path

#### Task 20 - Lua scripting hooks
- `ScriptSystem`: `lua_State` lifecycle, error reporting, `DS_DEV` reload
- Bind core API: spawn enemy, set wave config, query/modify entity, events
- Data-drive enemy stats (health/speed/damage) + wave layout from `.lua`
- Event callbacks: `onWaveStart`, `onEnemyDeath`, `onPlayerDeath`
- Sandbox: no io/os in shipped builds; keep binding layer thin
