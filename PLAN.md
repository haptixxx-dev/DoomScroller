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
- [x] Player health + i-frames (`HealthComponent`, pure `applyDamage` helper, enemy contact damage on cooldown, death + auto-respawn via `PhysicsWorld::setPosition`)
- [x] Audio (`AudioSystem` PIMPL over miniaudio: SFX/music buses, decoded-sound cache, 2D/3D one-shots, camera listener, looping music, safe no-op on missing files)
- [x] HUD / UI overlay (`UISystem` immediate-mode batcher: crosshair, health bar, weapon/wave readouts, damage flash + low-health pulse, YOU DIED banner, embedded 8x8 bitmap font, built-in white texture)
- [x] VFX / particle system (pooled CPU `ParticleSystem` + free list, `particle.slang` instanced camera-facing billboards, alpha + additive pipelines, muzzle/blood/spark/explosion presets)
- [x] Dynamic lights (`LightComponent`, forward up-to-16 point lights in `mesh.slang` via fragment push constants, std140 `LightBuffer`/`GpuLight`, decaying transient lights + `spawnTransientLight()`, muzzle-flash light)
- [x] Movement tech (accel/decel ground+air model, dash with charge bank + i-frames, slide via eye-offset drop, `MovementTuning` + pure `MovementTech.h` math, Shift=dash / Ctrl=slide)
- [x] Projectile weapons + switching (`ProjectileComponent`, `WeaponType` loadout, `ProjectileSystem` manual integration + ray-cast hit + splash falloff, 1..N / wheel switching, impact callback wires VFX/light/audio)
- [x] Wave spawner + game state (`GameState` machine, pure `WaveSystem` score/combo, `HighScore` persistence, wave -> intermission -> next loop, Menu/Dead/Victory overlays, spawn from `SpawnPoint`s)
- [x] Level format + loader (versioned `DSLV` binary, `LevelLoader` read/write/load into ECS + physics, `tools/level_convert` stub, `arena.dslv`, round-trip tests, `buildArena` fallback)
- [x] Lua scripting hooks (`ScriptSystem` owns `lua_State`, sandboxed stdlib, manual-bound `ds` table spawn/get/set/emit/wave-config, data-driven enemy stats + wave layout from `waves.lua`, `onWaveStart`/`onEnemyDeath`/`onPlayerDeath` callbacks)
- [x] Fix: Windows `LNK2038` runtime mismatch (`USE_STATIC_MSVC_RUNTIME_LIBRARY OFF` aligns Jolt's `/MT` to the project-wide dynamic `/MD`; first green Windows build since Jolt was first linked into an exe)
- [x] CI hygiene: pin to `clang-format` 18 (ubuntu-latest's version), `clang-tidy` narrowing/member-init clean — see `CLAUDE.md` "CI / toolchain gotchas"

### Next (in order)

Phase 2 (tasks 11-20) is **done** - two-way combat, audio/UI/VFX feedback, movement
tech, projectiles, waves/state, level format, and Lua hooks all landed green.

Phase 3 (tasks 21-45) is **done** - HDR/tonemap, render-graph, hot-reload, frustum
cull, game-feel (shake/recoil/hitstop), bloom, PBR, instancing, damage numbers,
enemy archetypes, sun shadows, style meter, pickups, RHICaps tier profile, parry,
ragdoll/gibs, alt-fire/upgrades, settings store + userDir, GPU compute particles,
boss, rebindable input map, text level parser, BC7 .dstex cook tool, settings menu
+ audio buses, and the SaveData blob all landed green (build + ctest, 34 test files).

**Verification ceiling note:** this machine has no GPU/window, so every rendering
feature (21, 22, 26, 27, 31, 28-instancing, 39-compute) is verified by build +
slangc shader-compile + tested CPU math references + adversarial code review, NOT
by running the binary. A first run on real hardware must visually confirm: HDR/ACES
brightness, PBR look, shadow orientation/bias, bloom intensity, instanced draws,
and the Enhanced-tier compute particle path. Five real bugs were caught by
adversarial verification and fixed during the build (SDL3 blend-factor INVALID,
shadow sun-direction inversion, hot-reload partial-failure handle leak, per-spawn
mesh-buffer leak, unclamped audio volumes). Known follow-ups: real `deviceVRAMBytes`
query in RHICaps (currently 0 → Vulkan/Linux runs the Minimum tier), a real BC7
encoder behind the `.dstex` cook seam (emits RGBA8 today), and the one-frame
GPU-particle position lead on the Enhanced tier.

See git history / `CLAUDE.md` for the per-task detail; the table below is retained
as the design record.
Phase 3 (tasks 21-45) is **done** (see PR / branch `feat/phase3-vertical-slice`) -
multi-pass renderer (HDR/tonemap, render-graph, hot-reload, bloom, PBR, sun
shadows), frustum culling, game feel (shake/recoil/hitstop), instancing, damage
numbers, enemy archetypes, style meter, pickups, RHICaps tier profile, parry,
ragdoll/gibs, alt-fire/upgrades, settings store + userDir, GPU compute particles,
boss, input map, text level parser, BC7 `.dstex` cook tool, settings menu + audio
buses, and the SaveData blob all landed green (build + ctest, 34 test files).

**Phase 3 verification ceiling:** this machine has no GPU/window, so every
rendering feature is verified by build + slangc shader-compile + tested CPU math
references + adversarial code review, NOT by running the binary. Five real bugs
were caught by adversarial verification and fixed during the build (SDL3
blend-factor INVALID, shadow sun-direction inversion, hot-reload partial-failure
handle leak, per-spawn mesh-buffer leak, unclamped audio volumes). Phase 4 closes
this loop.

Phase 4 (tasks 46-58) = trust what Phase 3 built, then ship and deepen it:
verification harness + real-hardware validation, CI/packaging to a downloadable
artifact, content/progression depth, and the Enhanced-tier GPU payoff the custom
RHI was designed for.

### Phase 4 (tasks 46-58)

**Execution order: A (46-48) -> 49 -> D (51-54) -> C (55-58), packaging (50) last.**

Ordered by dependency/risk. Wave A is a prerequisite for the rest: you cannot
tune or trust Phase 3's unseen render work without a way to see it, and a
screenshot-diff harness turns every later render change into a regression test.

| # | Task | Depends on | Key deliverable |
|---|------|-----------|-----------------|
| 46 | **Headless smoke target** | - | A no-GPU executable that constructs every pure system + RHI-less path; runnable in CI to catch link/ctor regressions the GPU build can't surface in the sandbox |
| 47 | **Golden-image test harness** | 46 | Offscreen-render fixed scenes via `debugDownloadTexture` -> PPM, diff vs committed reference images with a tolerance; first real pixel validation of HDR/PBR/shadows/bloom/instancing |
| 48 | **Real-hardware validation pass** | 47 | Run the binary on an actual GPU, capture reference images for 47, log + fix what is genuinely broken in the Phase 3 render paths (the five "unseen" features get truth-tested) |
| 49 | **CI expansion (finish task 45)** | 46 | CI builds the tools, runs the cook step, runs all ctest suites + the smoke target, gates clang-format/clang-tidy, uploads per-OS artifacts |
| 50 | **CPack per-OS packaging** | 49 | Windows installer/zip, macOS `.app` (+ Info.plist, Metal shaders), Linux AppImage/tarball, each staging cooked assets + compiled shaders into the `ds::paths` shipping layout |
| 51 | **Meta-progression / persistent unlocks** | - | Wire `SaveData.unlockFlags` + run stats into between-run persistent state; unlocks survive restarts and seed the menu |
| 52 | **Weapon unlock tree + economy** | 51 | Currency earned from style/kills, persisted unlock + upgrade choices, spend UI at intermission/menu |
| 53 | **Difficulty curve + more levels** | 51 | Authored levels through the text-parser -> cook pipeline; tuned wave/enemy escalation curve, selectable on the menu |
| 54 | **Enemy + boss variety** | 30, 40 | More archetypes + hazards; multi-boss / multi-pattern encounters; data-driven via Lua + archetype fields |
| 55 | **Real `RHICaps` device query** | - | Actual `deviceVRAMBytes` / mesh-shader / bindless detection from SDL3/native handles, replacing the stub that forces Vulkan/Linux to the Minimum tier |
| 56 | **BC7 encoder** | 55 | A real BC7 compressor behind the `.dstex` cook seam (replaces the RGBA8 stub), gated on `RHICaps`; `TextureManager` BCn upload path exercised |
| 57 | **Mesh-shader pipeline path** | 55 | The Enhanced-tier `VK_EXT_mesh_shader` / Metal mesh-shader path via `nativeDevice()`, tier-gated; the "why a custom engine" payoff |
| 58 | **Bindless materials + GPU compute depth** | 55, 57 | Bindless material/texture arrays; fix the one-frame GPU-particle position lead; expand GPU compute usage (culling / sim) on the Enhanced tier |

Recommended execution order: **A (46-48) -> CI half of B (49) -> D (51-54) ->
C (55-58)**, with packaging (50) near the end once there are real cooked assets
and a validated render to ship. Wave A makes everything else verifiable; the CI
half of B keeps it green; D ships player-facing value cheaply (mostly tested
logic/data) while real hardware is lined up for C + task 48.

#### Phase 4 task detail

#### Task 46 - Headless smoke target
- A CI-runnable executable (or ctest case) that constructs the pure systems +
  any RHI-independent engine paths without creating a GPU device/window
- Catches link errors, static-init order, and ctor regressions that the
  GPU-gated build cannot surface in a headless sandbox
- Foundation for the CI expansion (49)

#### Task 47 - Golden-image test harness
- Render a small set of fixed, deterministic scenes to an offscreen target and
  read them back with the existing `debugDownloadTexture` -> PPM path
- Diff against committed reference images with a per-pixel tolerance
- First automated validation that HDR/tonemap, PBR, shadows, bloom, and
  instancing actually produce the expected pixels (not just compile)
- Turns every future render change into a regression test

#### Task 48 - Real-hardware validation pass
- Run the binary on an actual GPU across the three backends where possible
- Capture the reference images task 47 diffs against
- Truth-test and fix the Phase 3 render features that have never been seen run:
  HDR/ACES brightness, PBR look, shadow orientation/bias, bloom intensity,
  instanced draws, the Enhanced-tier compute particle path
- Resolve the known Phase 3 caveats observed at runtime

#### Task 49 - CI expansion (finishes the deferred half of task 45)
- Build the offline tools (`level_convert`, `asset_cook`) in CI
- Run the cook step on sample assets; run all three ctest suites + the smoke
  target (46) with `--output-on-failure`
- Keep clang-format + clang-tidy gating; upload per-OS build artifacts

#### Task 50 - CPack per-OS packaging
- Windows installer/zip, macOS `.app` (Info.plist + Metal shaders), Linux
  AppImage/tarball
- Each stages the binary + cooked assets + compiled shaders into the
  binary-relative layout `ds::paths` expects in shipping mode
- Depends on the cook tool + a validated render so the artifact is worth shipping

#### Task 51 - Meta-progression / persistent unlocks
- Use `SaveData.unlockFlags` + run-stat fields for between-run persistent state
- Unlocks/stats survive restarts and seed the menu and economy
- Versioned + CRC'd already (task 45 blob); just wire the gameplay meaning

#### Task 52 - Weapon unlock tree + economy
- Currency from style score / kills, persisted via SaveData
- Unlock + upgrade choices (builds on the task-37 upgrade system) spent at a
  menu/intermission screen
- Drives replay value

#### Task 53 - Difficulty curve + more levels
- Author additional levels through the text-parser (42) -> cook pipeline
- A tuned wave/enemy escalation curve; level/difficulty selection on the menu

#### Task 54 - Enemy + boss variety
- More archetypes + arena hazards; multi-phase / multi-pattern boss encounters
- Data-driven through Lua + the archetype fields from task 30

#### Task 55 - Real RHICaps device query
- Query actual `deviceVRAMBytes` / mesh-shader / bindless support from SDL3 +
  native handles at device creation
- Replaces the Phase 3 stub that leaves VRAM 0 and forces Vulkan/Linux to the
  Minimum tier; makes the tier profile (34) reflect real hardware

#### Task 56 - BC7 encoder
- A real BC7 compressor (new extern: bc7enc / ispc-texcomp) behind the `.dstex`
  cook seam, replacing the RGBA8 placeholder
- Gated on `RHICaps`; exercise the `TextureManager` BCn upload path + DS_DEV
  RGBA8 fallback

#### Task 57 - Mesh-shader pipeline path
- The Enhanced-tier mesh-shader render path (Vulkan `VK_EXT_mesh_shader` / Metal
  mesh shaders) via the `nativeDevice()` escape hatch, tier-gated
- The core "why a custom RHI" payoff from the original design

#### Task 58 - Bindless materials + GPU compute depth
- Bindless material/texture arrays to cut per-draw binding
- Fix the one-frame GPU-particle position lead from task 39
- Expand GPU compute usage (e.g. culling / simulation) on the Enhanced tier

### Phase 3 (tasks 21-45)

| # | Task | Depends on | Key deliverable |
|---|------|-----------|-----------------|
| 21 | **Offscreen HDR target + tonemap** | - | Scene -> RGBA16F target, fullscreen `tonemap.slang` (ACES/Reinhard + exposure) -> swapchain before UI |
| 22 | **RHI hot-reload shader path** | - | DS_DEV `.slang` filesystem watcher -> `slangc` recompile -> in-place pipeline reissue |
| 23 | **Render graph / frame-pass scheduler** | 21 | Declarative pass list (inputs/outputs, load/store, record lambda), transient RT alloc; migrate scene/particle/HUD/tonemap |
| 24 | **CPU frustum culling** | - | 6 view-proj planes (pure, tested) + mesh AABBs, skip off-screen draws in `renderSystem` |
| 25 | **Camera juice: shake + recoil + hitstop** | - | Pure tested `GameFeel.h` trauma shake, additive recoil, hitstop `m_dt` scale; driven by fire/hit/kill |
| 26 | **Bloom + final post stack** | 23 | Bright-pass + separable Gaussian down/up chain (`post.slang`), additive composite in tonemap, tunables |
| 27 | **PBR metallic-roughness materials** | 21 | Cook-Torrance GGX in `mesh.slang`, albedo/MR/normal textures (cgltf), feeds point-light loop + HDR |
| 28 | **GPU-instanced static & enemy batching** | 24 | Group identical mesh+material into one `drawIndexed` with per-instance model-matrix binding |
| 29 | **Damage numbers + hitmarkers** | 25 | Hit/kill events -> world-to-screen floating damage numbers + crosshair hitmarker / kill-confirm |
| 30 | **Enemy archetypes + AI variants** | - | `EnemyArchetype` (Grunt/Charger/Ranged) data-driven fields, FSM branches, Lua-weighted spawns |
| 31 | **Sun shadow map (single cascade)** | 23, 27 | Depth-only pass into D32F from sun light-space matrix, PCF sample in `mesh.slang` |
| 32 | **ULTRAKILL style/rank meter** | 29 | Decaying style score (variety/air/dash/multi-kills) -> D..SSS rank, HUD bar; pure tested math |
| 33 | **Pickups: health / ammo / dash orbs** | 30 | `PickupComponent` + proximity `pickupSystem`, spinning billboards, audio/VFX cues, drop on kill |
| 34 | **RHICaps tier detection + quality profile** | 26, 31 | Populate caps (VRAM/mesh-shader/bindless), pick Minimum vs Enhanced profile toggling graph passes |
| 35 | **Parry + dash-cancel** | 30, 32 | Parry window negates damage + reflects Ranged projectile + refunds dash; dash-cancels recovery; tested timing |
| 36 | **Ragdoll / death-gib feedback** | 25 | `PhysicsWorld::addDynamicBox`/`removeBody`, impulse-launched gib + blood/explosion emit, timed despawn |
| 37 | **Weapon alt-fire + upgrade modifiers** | 33 | Right-mouse alt-fire variants + stackable `WeaponUpgrade` multipliers granted at intermission, HUD readout |
| 38 | **Settings store + user-dir resolver** | - | `ds::paths::userDir()` (SDL_GetPrefPath) + versioned `SettingsStore` (gfx/audio/input), tested parse/serialize |
| 39 | **GPU compute particle simulation** | 34 | `IRHICommandList::dispatch` + `compMain` particle integration into instance buffer; Enhanced-tier, CPU fallback |
| 40 | **Boss encounter with phased AI** | 30, 35 | `BossComponent` + `bossSystem` multi-phase volleys/charges/vulnerable windows as final wave, boss health bar |
| 41 | **Rebindable input + gamepad** | 38 | `InputMap` action->scancode/button (pure tested) + SDL_Gamepad analog move/look, deadzone, persisted via settings |
| 42 | **Text level parser + data-driven placement** | - | Parser for text/Lua level desc -> `.dslv` in `level_convert`; consume `SpawnPoint.flags` + `LightRecord` in loader |
| 43 | **Texture cook tool + .dstex BC7** | - | `tools/asset_cook` (stb + BC7enc) -> versioned `.dstex`; `TextureManager` + `TextureFormat::BC7Unorm` load path, DS_DEV RGBA8 fallback |
| 44 | **Settings/rebind menu + audio buses + feedback layer** | 32, 35, 41 | UISystem settings/pause sub-screen (sliders/toggles/rebind capture), named mix buses + ducking, SFX/VFX for new mechanics |
| 45 | **CI expansion + packaging** | 42, 43 | CI builds tools, runs cook + all ctest suites + format/tidy gating + artifacts; CPack per-OS installers staging cooked assets |

#### Task 21 - Offscreen HDR target + tonemap pass
- Render the 3D scene into an RGBA16F color target (`createTexture` render-target) + existing depth
- Add a fullscreen `tonemap.slang` pass (Reinhard/ACES + exposure) resolving HDR -> swapchain BGRA8
- Runs before UI/HUD so overlay stays LDR and crisp
- Establishes the multi-pass scaffold every later effect hangs off
- Carries forward task-14 followup: HDR target enables real glow for muzzle/plasma/explosions

#### Task 22 - RHI hot-reload shader path (DS_DEV)
- Filesystem watcher over `shaders/` via `ds::paths::shaderSources()`
- On change, invoke `slangc` to recompile the affected `.slang`
- Rebuild affected pipelines in place via a small pipeline registry / `createPipeline` reissue
- DS_DEV only, compiled-out no-op in shipping
- Realizes the "Hot-reload (future)" item in the shader-pipeline decided section

#### Task 23 - Render graph / frame-pass scheduler
- Lightweight pass list: name, RHITexture inputs/outputs, load/store ops, record lambda into `IRHICommandList`
- Owns transient render-target allocation and pass ordering
- Migrate scene / particle / HUD / tonemap passes onto it
- Centralizes attachment wiring so shadows / bloom slot in declaratively
- Foundation for tasks 26 (bloom), 31 (shadows), 34 (tier toggles)

#### Task 24 - CPU frustum culling
- Extract camera's 6 view-proj planes (pure, testable in `engine_math`)
- Add AABBs to `MeshComponent` (or a new `BoundsComponent`)
- `renderSystem` skips Transform+Mesh draws fully outside the frustum
- Catch2 plane-extraction + AABB-vs-frustum test
- Cheap win on the i5 min tier as level/enemy counts grow

#### Task 25 - Camera juice: screenshake + recoil + hitstop
- Pure `engine/include/engine/GameFeel.h` (unit-tested like `MovementTech`)
- Decaying-trauma screenshake (deterministic noise, no mid-frame RNG seeding)
- Additive recoil pitch/yaw offsets; hitstop timer scaling `m_dt`
- Engine applies shake/recoil to camera before building viewProj in `render()`
- Trauma from `fireWeapon()` / player-hit; hitstop on kill/explosion (respects existing `.05` `m_dt` clamp)

#### Task 26 - Bloom + final post stack
- Bright-pass threshold + separable Gaussian downsample/upsample chain (`post.slang`) reading the HDR target
- Composited additively in the tonemap pass
- Intensity / threshold exposed as tunables
- Gives muzzle flashes, plasma, and explosions real glow
- Slots into the render graph as a dedicated pass group

#### Task 27 - PBR metallic-roughness materials
- Extend `MaterialComponent` + `mesh.slang` to Cook-Torrance metallic-roughness (GGX + Smith + Fresnel)
- Read albedo / metallic-roughness / normal textures (cgltf already parses PBR)
- Feeds the existing point-light loop, output to the HDR target
- Replaces the Lambertian + hardcoded-sun lighting
- Keeps the directional sun term for the shadow task to shadow

#### Task 28 - GPU-instanced static & enemy batching
- Group identical mesh+material entities (arena tiles, enemy boxes, projectiles) into one `drawIndexed`
- Per-instance model-matrix vertex binding (`VertexBinding.instanced=true` already exists)
- Replaces the one-draw-per-entity loop in `RenderSystem`
- Cuts draw-call count for large waves on the min tier
- Builds on the culling pass (only batch what survives the frustum test)

#### Task 29 - Damage numbers + hitmarker feedback
- Surface a damage event on each enemy-health subtraction (hitscan `castRay`, projectile direct/splash)
- Engine turns events into UISystem world-to-screen floating damage numbers
- Brief crosshair hitmarker flash + a distinct kill-confirm marker
- Reuses `UISystem::drawText`/`drawQuad` and `m_camera` viewProj for projection
- Pairs with task-25 juice for satisfying hit feedback

#### Task 30 - Enemy archetypes via data-driven EnemyComponent + AI variants
- `EnemyArchetype` enum + per-type fields on `EnemyComponent` (Grunt melee, Charger rush, Ranged shooter)
- Branch the FSM in `EnemySystem`: Ranged fires a `ProjectileComponent` at range; Charger telegraphed lunge
- Spawn weights flow from Lua `waves.lua` / `ScriptEnemyStats` (designer-tunable, no code)
- Carries forward task-20 followup: enrich the script-driven enemy stat plumbing
- Foundation for pickups (33), parry (35), boss (40), difficulty (45/curve)

#### Task 31 - Sun shadow map (single directional cascade)
- Depth-only shadow pass rendering scene into a D32Float texture from the sun's light-space matrix
- Sample with PCF in `mesh.slang` to shadow the directional term
- One cascade sized for the arena
- Requires the render graph to own the extra depth pass
- Builds on PBR so the shadowed term is the same directional light

#### Task 32 - ULTRAKILL-style style/rank meter
- Extend `WaveState` (or sibling pure `StyleSystem` in `WaveSystem.h`) with a decaying style score
- Rewards variety: weapon-switch kills, airborne kills, dash-kills, splash multi-kills
- Maps to letter ranks D..SSS; HUD renders rank + bar top-right alongside COMBO
- Pure math kept testable; fed kill-context flags (combo, weapon type, airborne, dashedThisFrame)
- Consumes the damage/kill events surfaced in task 29

#### Task 33 - Pickups: health, ammo, and dash-charge orbs
- `PickupComponent {kind, value}` + `pickupSystem` proximity check (reuse `PhysicsWorld::getPosition`, no new Jolt query)
- Effects: heal `HealthComponent`, refill weapon ammo, grant a PlayerController dash charge
- Spawns a small spinning billboard/box mesh, fires existing audio/VFX cues
- Wave spawner drops them on enemy death via the kill loop
- Tunable drop rates routed through the archetype/Lua data

#### Task 34 - RHICaps tier detection + auto quality profile
- Populate `RHICaps` (deviceVRAMBytes, meshShaders, bindless) from SDL3/native queries at device creation
- Pick Minimum (GTX 1060: bloom-lite, no shadows, half-res post) vs Enhanced (full stack)
- Toggle render-graph passes per profile
- Implements the PLAN's runtime tier selection
- Depends on bloom + shadows existing so there are passes to gate

#### Task 35 - Parry + dash-cancel mechanic
- Treat a melee/parry input during an enemy Attack/projectile window as a parry
- Negate incoming damage, reflect a Ranged enemy's `ProjectileComponent` (flip velocity, swap owner), refund a dash charge
- Dash-cancel lets a dash interrupt slide/recovery
- Parry timing window added to `MovementTuning` (pure, tested) + a HUD parry-flash
- Depends on archetypes (reflectable projectiles) and the style meter (parry rewards style)

#### Task 36 - Ragdoll / death-gib feedback on enemy kill
- On enemy death, convert into short-lived dynamic Jolt bodies (min: impulse-launched box) + blood/explosion emit
- New `PhysicsWorld::addDynamicBox` + the long-noted-missing `PhysicsWorld::removeBody` for cleanup
- Carries forward task-18 followup: removeBody also cleans orphaned enemy capsules on wave clear/restart
- Keeps RHI box-mesh rendering; bodies despawn on a timer to bound count for low-end tiers
- Driven by the same kill loop and juice as tasks 25/29

#### Task 37 - Weapon alt-fire + upgrade modifiers
- Extend `WeaponComponent` with a right-mouse alt-fire variant (shotgun spread, charged rocket, plasma overheat)
- Stackable `WeaponUpgrade` set (damage / firerate / splash multipliers) applied in `fireWeapon()` / spawn
- Upgrades granted between waves (intermission choice), shown in HUD weapon readout
- Builds on pickups/intermission economy
- Feeds the difficulty curve's upgrade-offer cadence (task 45-curve work)

#### Task 38 - Settings store + Paths user-dir resolver
- Add `ds::paths::userDir()` (SDL_GetPrefPath)
- `SettingsStore` serializes graphics / audio / input config to versioned settings file under it
- Loaded at Engine construction before device/audio init
- Pure parse/serialize logic in a header-only piece testable via `engine_headers`
- Foundation for input rebind (41) and the settings menu / buses (44)

#### Task 39 - RHI compute dispatch + GPU particle simulation
- Add `IRHICommandList::dispatch` + a compute pass type (`ShaderStage::Compute` + Storage buffers already exist)
- Move ParticleSystem integration to a `compMain` in `particle.slang` writing the instance buffer on-GPU
- Removes the per-frame synchronous `uploadImmediate` stall noted in task 14
- Enhanced tier only; CPU path stays as fallback (gated by the tier profile)
- Carries forward task-14 followup on the synchronous particle re-upload

#### Task 40 - Boss encounter with phased AI
- `BossComponent {phase, phaseHealthThresholds, attackPattern}` + `bossSystem`
- Multi-phase fight: telegraphed projectile volleys (reuse `ProjectileSystem`), charge attacks, parryable vulnerable window
- Spawned as the final wave when `m_wave` reaches maxWaves instead of immediate Victory
- HUD boss health bar; defeat transitions to Victory
- Depends on archetypes (AI building blocks) and parry (vulnerable-window design)

#### Task 41 - Rebindable input + gamepad support
- `InputMap` action enum -> SDL scancode / mouse-button, queried by `processEvents`/`update` instead of hardcoded keys
- Defaults + serialization through `SettingsStore` so bindings persist; action-resolution factored pure for tests
- Extend to SDL_Gamepad axes/buttons (open in Engine ctor, handle add/remove)
- Analog move/look into PlayerController + dash/slide/fire actions, deadzone + look-sensitivity tunables
- Carries forward task-16 followup: PlayerController input now flows through the map

#### Task 42 - Text level description + level_convert parser
- Replace `level_convert`'s hardcoded `buildArenaLevel` with a parser for a simple text/Lua level description (boxes, spawns with player-start flag, lights) emitting `.dslv`
- Consume `SpawnPointRecord.flags` + `LightRecord` in `LevelLoader` so player start and `LightComponent`s come from data
- Carries forward task-19 followups: stop relying on Engine spawn/light constants
- Carries forward task-15 followup: data-driven `LightComponent` placement
- Read spawn points back to position player/enemies

#### Task 43 - Texture cook tool + .dstex BC7 format
- `tools/asset_cook` loads source PNGs (stb), generates mips, BC7-compresses (bc7enc / ispc-texcomp as a new extern), writes versioned `.dstex`
- Extend `TextureManager` to load `.dstex` and upload BCn via new `TextureFormat::BC7Unorm` in RHITypes + SDL3 backend
- DS_DEV: fall back to runtime stb RGBA8 when a cooked `.dstex` is absent; shipping requires cooked
- Gate the BC7 path on RHICaps so GTX 1060-tier and Metal both get a valid format
- Document the cook step in a tools README

#### Task 44 - Settings/rebind menu + audio buses + new-mechanic feedback
- UISystem settings/pause sub-screen on the Menu GameState: volume + look-sensitivity sliders, toggles, "press a key" rebind capture writing back through `SettingsStore`
- Surface AudioSystem's SFX/music buses as named volumes driven by settings + a UI bus + music ducking on YOU DIED/Victory
- Wire the audio/VFX gaps flagged in the digest: explosion SFX, parry chime, pickup cue, rank-up sting, footsteps from PlayerController movement state
- Add missing asset path constants; rely on AudioSystem's safe no-op-on-missing behavior
- Depends on settings store + input rebind + gamepad; consumes style (32), parry (35), pickups (33) events

#### Task 45 - Save system, CI expansion, and per-OS packaging
- Generalize `HighScore` into a versioned binary `SaveData` blob (best wave, unlocks, run stats, settings ref) under `userDir()` with magic+version+CRC; round-trip + version-reject tests
- Extend `ci.yml`: build tools (`level_convert`, `asset_cook`), run the cook step, run all three ctest suites `--output-on-failure`, gate clang-format/clang-tidy, upload per-OS artifacts; add a headless smoke target constructing pure systems without a GPU
- CPack (or scripts): Windows installer/zip, macOS `.app` (Metal shaders + Info.plist), Linux tarball/AppImage
- Each stages binary + cooked assets + compiled shaders into the binary-relative layout `ds::paths` expects in shipping mode
- Depends on the cook tool + text level parser so CI/packaging have real cooked assets to stage

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
