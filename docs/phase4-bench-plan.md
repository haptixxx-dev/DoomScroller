# Phase 4 — GPU/bench-bound task plan

**Scope:** the Phase 4 tasks that can only be *truth-tested* on real hardware
(48, 55, 57, 58, 59) plus packaging (50) and the deferred Wave-D runtime wiring.
The sandbox has no GPU/window; verification ceiling here is **build green +
ctest pass + slangc compile**. This doc splits every task into a **Verifiable-here**
half (pure seams that can land as sandbox PRs, build+ctest gated) and a
**Needs-bench** half (wiring + visual verify), so the on-bench sessions are as
short as possible.

Grounding is by `file:line`. PLAN task defs are authoritative (`PLAN.md`
tasks 48/50/55/57/58/59, dep table lines 209–224). Conventions: `CLAUDE.md`.

**Global execution order** (respects PLAN deps 55→57/58/59, 48 first):
`48 → 55 → 57 → 58 → 59 → 50`. 48 unblocks trust in the whole Phase-3 render;
55 populates real caps so 57/58/59 tier-gating is meaningful; 50 ships once the
render is validated and assets cook.

Already landed (do not redo): the pure golden-image core
(`engine/include/engine/GoldenImage.h`, tested by `tests/test_golden_image.cpp`)
and the headless smoke target (`tests/smoke_main.cpp`, `tests/CMakeLists.txt:58`,
`add_test ds_smoke` at `tests/CMakeLists.txt:72`) — tasks 46/47's pure halves.

---

## Task 48 — Real-hardware validation pass

**Execution order: FIRST.** Everything downstream (55/57/58/59 visual verify,
50 "worth shipping") assumes a render you can trust. Also the capture path this
task fixes is the *input* to the task-47 harness.

### The capture-path bug (blocker, must fix before any HDR capture)
`SDL3Device::debugDownloadTexture` (`engine/src/rhi/sdl3/SDL3Device.cpp:589`)
is hard-wired to LDR RGBA8/BGRA8:
- `const uint32_t bytes = w * h * 4;` (`:590`) — fixed 4 bytes/px. An
  `RGBA16Float` HDR target (`m_hdrTexture`, format `RGBA16Float`,
  `RHITypes.h:53`) is **8 bytes/px**, so this under-allocates the transfer
  buffer by half and reads garbage.
- `isBGRA` is keyed off `SDL_GetGPUSwapchainTextureFormat(...)` (`:618`), i.e.
  the **swapchain** format, not the *source texture's* format. Reading back an
  offscreen R32Float / RGBA16F / RGBA8 target swizzles by the wrong rule.
- The `px[i*4+0..2]` unpack (`:626`) assumes 8-bit channels; F16 needs a
  half→float→8-bit-sRGB(or clamp) conversion before it can produce a P6 byte
  identical to `writePpm` (`GoldenImage.h:98`).

**Fix shape:** thread the *source* `TextureFormat` into `debugDownloadTexture`
(pass it in, or store per-texture format in the handle). Branch on format:
compute `bytesPerPixel` (RGBA8/BGRA8=4, R32F=4, RGBA16F=8), pick swizzle from
the *source* format (BGRA8 swaps, RGBA8/F16 don't), and for F16 do
half-decode + tonemap-to-8bit so the golden image captures the *post-tonemap
LDR swapchain* rather than raw HDR. Simplest correct route: **always capture
the final swapchain/LDR tonemap output** (BGRA8/RGBA8), not the HDR target —
then the existing 4-bpp path is correct and F16 readback is never needed. Capture
HDR separately only if a raw-HDR golden is wanted.

### What to capture (feeds task-47 golden references)
Deterministic fixed scenes rendered offscreen → `debugDownloadTexture` → PPM,
committed as references, diffed by `compareImages` (`GoldenImage.h:158`,
tolerance 2–4 to absorb driver variance — the harness already supports this).
Capture the final tonemapped LDR frame for each, on each backend where possible
(Vulkan/Linux, D3D12+DXIL/Win, Metal/mac).

### The 5 unseen Phase-3 features — what "correct" looks like
Config anchors are real; use them to know what to expect.

1. **HDR / ACES tonemap brightness.** Curve is ACES filmic mirrored in
   `engine/Tonemap.h:46` (a=2.51 b=0.03 c/d=2.43 e=0.59 f=0.14), `exposure=1.0`
   default (`Engine.cpp:2811`, `tonemap.slang:55`). *Correct:* no clipped white
   blowout on the arena; bright emissives (muzzle/plasma) roll off to white, not
   hard-clip; midtones sit filmic-dark, not washed. A linear/no-tonemap bug reads
   as flat/oversaturated.
2. **PBR look.** Cook-Torrance in `mesh.slang` fed by the point-light loop
   (task 27). *Correct:* metallic surfaces show tight specular highlights that
   track the light; rough surfaces broad/dim; no energy blow-up (highlight
   brighter than the light). Watch for inverted roughness (rough looks mirror).
3. **Sun shadow orientation / bias.** `kSunDir = normalize(0.5,1,0.3)`
   (`Engine.h:203`); shadow cam uses `-kSunDir` (`Engine.cpp:2606`), frustum
   sized to `m_levelBounds`. *Correct:* shadows fall *away* from the sun
   (down-and-toward -x,-z); attached to the caster's base (no peter-panning
   gap), no acne stripes on lit floors. A sign flip reads as shadows on the wrong
   side or full-bright; wrong bias reads as acne or floating shadows.
4. **Bloom intensity.** `kBloomThreshold=1.0 kBloomKnee=0.5 kBloomIntensity=0.6`
   (`Engine.h:267–269`), composited pre-ACES (`tonemap.slang:69`). *Correct:*
   only >1.0 HDR pixels bloom (arena albedo shouldn't); muzzle/plasma/explosion
   glow softly; no full-screen haze (threshold too low) and no hard ring
   (upsample chain wrong).
5. **Enhanced-tier compute particles.** `computeParticles` gate
   (`QualityProfile.h:39`, tier from caps). Blocked in the sandbox because caps
   force Minimum (see task 55). *Correct:* GPU path matches the CPU path
   visually — **except** the one-frame lead (fixed in task 58). Capture blood/
   spark/explosion bursts and diff the compute vs CPU instance output.

### Verifiable-here
- **The `debugDownloadTexture` format fix is partially pure.** The
  bytes-per-pixel + swizzle-select logic can be extracted to a free function
  (e.g. `bytesPerPixel(TextureFormat)` and a `swizzleToRGB(fmt, src, dst)`) in a
  header and unit-tested against synthetic byte buffers with **no GPU** — feed it
  a fake RGBA8/BGRA8/R32F row and assert the emitted RGB matches. That shrinks
  the bench change to "call the pure helper + the F16 decode."
- Golden-image diff logic already tested (`test_golden_image.cpp`); no new pure
  work needed there.

### Needs-bench
- Running the binary per backend; the actual capture; committing reference PPMs;
  the F16→8bit tonemap decode (only if raw-HDR goldens are wanted); every "what
  correct looks like" visual judgement above.

### Risks
- Capturing the *HDR* target instead of the tonemapped LDR swapchain makes
  golden diffs backend-fragile (F16 rounding differs per driver). Prefer LDR.
- Backend swizzle differences (BGRA vs RGBA swapchain) must key off the *source*
  texture, or Linux/Win references silently disagree channel-order.

---

## Task 55 — Real `RHICaps` device query

**Execution order: SECOND (before 57/58/59 — PLAN dep).** Today caps are a stub:
`m_caps.deviceVRAMBytes = 0` (`SDL3Device.cpp:55`), and mesh/bindless are
*guessed* from the shader format (`:50–54`: DXIL or MSL ⇒ Enhanced, SPIRV ⇒
flags left false). Consequence: **Vulkan/Linux runs the Minimum tier** —
no shadows, no compute particles — regardless of the actual GPU. This is why the
Enhanced path is untested.

### File-level steps
- **`RHITypes.h:220` (`RHICaps`)** already has the fields
  (`meshShaders/bindless/deviceVRAMBytes/maxTextureDim/maxPushConstBytes`). No
  struct change needed for the basics; add a `bool computeSupported` /
  `uint32_t vendorId` if the query exposes them.
- **`SDL3Device` ctor (`:32–55`)** is where population happens. Replace the
  shader-format heuristic with real detection:
  - **VRAM:** SDL3 GPU has no VRAM query, so drop through `nativeDevice()`
    (`SDL3Device.h:95` returns `m_gpu`) to the backing API. On Vulkan:
    `vkGetPhysicalDeviceMemoryProperties` → sum `DEVICE_LOCAL` heaps. On D3D12:
    `IDXGIAdapter3::QueryVideoMemoryInfo`. On Metal:
    `MTLDevice.recommendedMaxWorkingSetSize`. Reaching the native handle needs
    an SDL3 driver-name check (`SDL_GetGPUDeviceDriver`) + the matching native
    getter; SDL3 exposes native device handles per backend.
  - **Mesh shaders:** query the real extension —
    Vulkan `VK_EXT_mesh_shader` presence, Metal `supportsFamily(Metal3)`,
    D3D12 `D3D12_FEATURE_D3D12_OPTIONS7.MeshShaderTier`.
  - **Bindless:** Vulkan `VkPhysicalDeviceDescriptorIndexingFeatures`, D3D12
    resource-binding tier ≥ 3, Metal argument buffers tier 2.
- Keep the conservative fallback: if native probing fails, leave `0`/`false`
  (still forces Minimum — never over-promises, matching the current comment at
  `:39`).

### Verifiable-here (PURE SEAM — high value)
The caps→tier→profile mapping is **already pure and tested**:
`QualityProfile.h` (`selectTier` `:60`, `profileForTier` `:73`,
`profileForCaps` `:101`) depends only on `RHITypes.h`; `test_quality.cpp`
covers the VRAM threshold, mesh/bindless overrides, and the 0-VRAM
conservative case (8 cases, `test_quality.cpp:8–103`). So:
- **Extend the mapping in the sandbox** without a device: e.g. add an
  intermediate tier, a `vendorId`-based override, or a "confirmed VRAM but no
  mesh/bindless" rule, and land the matching `test_quality.cpp` cases. Pure,
  build+ctest gated.
- **A pure `capsFromRawQuery(...)` normalizer** could sit between the native
  probe and `RHICaps` (clamp/sanitize VRAM, OR feature bits), and be unit-tested
  against synthetic inputs — moving the only testable slice of the device query
  off the bench.

### Needs-bench
- The native-handle probing itself (per backend), and confirming Linux/Vulkan
  now selects Enhanced on an RTX-class card and stays Minimum on a GTX 1060.

### Risks
- `selectTier` currently returns Enhanced if `meshShaders || bindless` **OR**
  VRAM≥6GiB (`QualityProfile.h:61–67`). Once real bits arrive, a card with
  bindless but 3GB VRAM (some laptop parts) would go Enhanced and may thrash the
  VRAM budget. Consider requiring VRAM≥threshold *AND* a feature bit, and add the
  test case in the sandbox first.
- Reaching native handles ties the RHI to specific backends inside the one file
  that's *allowed* to (`ShaderLoader`/`nativeDevice` escape hatch) — keep it in
  `SDL3Device.cpp`, never leak into `engine/`.

---

## Task 57 — Mesh-shader pipeline path

**Execution order: THIRD (needs 55's real mesh-shader flag to gate on).** The
"why a custom RHI" payoff.

### How the escape hatch works today
- `nativeDevice()` returns the raw `SDL_GPUDevice*` (`SDL3Device.h:95`,
  `IRHIDevice.h:66`); engine reaches it only for `ShaderLoader`
  (`Engine.cpp:346/822/894`) and `UISystem` init (`:802`) — the sanctioned
  exception per `CLAUDE.md`.
- Graphics pipelines are built in `SDL3Device::createPipeline`
  (`SDL3Device.cpp:257+`) from a vertex+fragment `PipelineDesc`
  (`RHITypes.h:162`). SDL3 GPU has **no** mesh-shader entry point, so the mesh
  path cannot go through `createPipeline` — it must be built directly against the
  native backend (Vulkan `VkPipeline` with `VK_EXT_mesh_shader` /
  Metal mesh pipeline) behind the `nativeDevice()` handle, tier-gated.

### File-level steps
- **Author the shaders (pure/compile-verifiable):** add `mesh_ms.slang`
  (task/mesh stages) to the `ds_compile_slang` SOURCES in root
  `CMakeLists.txt:107` (per `CLAUDE.md`, un-listed shaders never ship). Slang can
  emit mesh-shader SPIR-V/MSL; compile is verifiable in the sandbox even though
  the pipeline can't be created.
- **Add a native mesh-pipeline builder** in `SDL3Device.cpp` (or a sibling TU
  it owns) reached only when `caps().meshShaders` is true, using the native
  device/queue handles. Gate all of it behind `m_quality.tier == Enhanced` and
  a runtime `caps().meshShaders` check.
- **Wire a tier switch in the scene pass** (`Engine.cpp` scene-pass record,
  around `:2708+`): if mesh path available, record mesh-shader draws for static
  batches; else fall back to the existing indexed draw. The forward/Minimum path
  stays untouched.

### Verifiable-here
- **Shader authoring + slangc compile** of the mesh/task shader (build target).
- **Tier-gating logic** — a pure predicate `bool useMeshShaders(profile, caps)`
  in a header, unit-tested (mirrors `test_quality.cpp` style): Enhanced+meshbit
  ⇒ true, everything else ⇒ false.

### Needs-bench
- Native `VkPipeline`/Metal mesh pipeline creation, the meshlet build, the actual
  draw, and confirming pixel-parity with the indexed path (diff against a
  task-47 golden of the same scene).

### Risks
- Meshlet-ized geometry can subtly differ from indexed draws (winding, culling),
  breaking the golden diff for the *same* scene. Capture a mesh-path golden
  separately rather than reusing the forward one.
- Native pipeline objects have their own lifetime; ensure they're destroyed in
  `~SDL3Device` alongside `SDL_GPUGraphicsPipeline` handles.

---

## Task 58 — Bindless materials + GPU compute depth

**Execution order: FOURTH (PLAN dep 55, 57).**

### The one-frame GPU-particle lead (concrete bug, isolatable)
Frame order today:
1. `m_particles.update(m_dt)` integrates the CPU pool to frame N
   (`Engine.cpp:2333`).
2. `dispatchParticleCompute` uploads that pool and the compute shader
   **extrapolates another `dt` step**: `pos = p.position + (vel)*dt`
   (`particle_sim.slang` `compMain`, `Engine.cpp:2927–2947`).

So the GPU-drawn instance positions lead the true CPU positions by one frame.
The **CPU fallback** path does *not* extrapolate — it draws
`m_particles.buildInstances()` directly (`Engine.cpp:3144`) — so the two paths
visibly diverge by one frame's motion. `particle_sim.slang`'s own header comment
even admits it extrapolates because the state buffer is read-only.

**Fix shape:** either (a) make the shader a pure pass-through (write
`p.position` unchanged, drop the `+ vel*dt`), integrating only on the CPU like
the fallback — cheapest, exact parity; or (b) integrate *only* on the GPU
(don't advance the CPU pool's position before upload) and read state back. (a)
matches the CPU path and is the obvious correct fix.

### Bindless materials
- `RHICaps.bindless` (`RHITypes.h:224`) already exists and gates Enhanced.
- Material/texture binding today is per-draw (`bindFragmentTexture`,
  `SDL3CommandList` `SDL3Device.cpp:751`). Bindless means a texture *array* +
  per-instance material index. **This needs the cube/array texture RHI work from
  task 59** (there's no array-texture path yet), so sequence bindless *after* or
  *with* 59's `TextureDesc` type field.

### Verifiable-here (PURE SEAM — high value, low effort)
- **The particle one-frame-lead fix is testable on the CPU.** `ParticleSystem`
  is a pure pooled system (`tests/test_particles.cpp` already exists). Add a test
  asserting the compute-fed instance positions equal the CPU `buildInstances()`
  positions for the same pool + dt (i.e. the two code paths agree). If the
  extrapolation is removed from the shader, the *CPU-side* `buildGpuParticles()`
  / `gpuParticles()` snapshot (`Engine.cpp:2927–2930`) must already carry the
  final position — assert that. Slang recompile confirms the shader change builds.
- **A pure material-index packing helper** (instance→material-slot table) can be
  written and tested headless before any GPU array binding exists.

### Needs-bench
- Confirming compute vs CPU parity *on screen* (diff a task-47 particle golden).
- The bindless texture-array binding + per-instance material index draw path.
- Any additional compute-culling work (GPU frustum cull writing an indirect
  draw buffer) is fully GPU.

### Risks
- Removing the shader extrapolation while leaving the CPU pool advanced (or vice
  versa) just moves the lead by a frame in the other direction — the test must
  pin *parity with the CPU fallback*, which is the ground truth.
- Bindless is backend-capability-sensitive; keep it gated on real
  `caps().bindless` (task 55) so Minimum/GTX-1060 never hits it.

---

## Task 59 — Real lighting overhaul

**Execution order: FIFTH / LAST of the C-wave (PLAN: highest-risk, most GPU-bound;
needs 55/57's capability pattern).** This is a *stack* of sub-tasks; the RHI
cube/array support is the prerequisite for the rest.

### Confirmed ceilings in the current RHI (read, not assumed)
- `TextureDesc` has **no texture-type field** — only `width/height/depth/
  mipLevels/format/isRenderTarget/isDepthStencil` (`RHITypes.h:96–105`).
- `SDL3Device::createTexture` **hardcodes 2D**:
  `info.type = SDL_GPU_TEXTURETYPE_2D;` (`SDL3Device.cpp:145`).
- `RenderPassDesc` has **no layer/face/mip targeting** — just
  `colorAttachments` + one `depthAttachment` (`RHITypes.h:212–215`); the
  attachment structs (`ColorAttachment` `:197`, `DepthAttachment` `:204`) carry
  only a texture handle + load/store, no `layer`/`face`/`mipLevel`. So a render
  pass can't target face N of a cube map.
- **Exactly ONE point light shadow-casts per frame** — the nearest to the camera,
  chosen by a linear distance scan (`Engine.cpp:2618–2639`), rendered to 6
  separate `R32Float` 2D "cube-face" color targets
  (`m_pointShadowFaces`, `Engine.cpp:2681–2706`) as a workaround for having no
  cube maps. Cube-face matrices come from `ShadowMatrix.h`'s
  `pointShadowFaceMatrix` (`:131`).
- Sun shadow is a **single cascade** sized to `m_levelBounds`
  (`sunLightSpaceMatrix`, `ShadowMatrix.h:53`; used at `Engine.cpp:2606`).
- `kMaxForwardLights = 16` (`ecs/RenderSystem.h:16`); CPU gather caps at 16
  (`updateLights`, `Engine.cpp:3196–3221`).

### File-level steps
1. **RHI cube/array support (prereq):** add `enum TextureType {Tex2D, TexCube,
   Tex2DArray}` + `arrayLayers` to `TextureDesc` (`RHITypes.h:96`); add
   `uint32_t layer`/`mipLevel` to `ColorAttachment`/`DepthAttachment`
   (`RHITypes.h:197/204`). Honour them in `createTexture`
   (`SDL3Device.cpp:145` — pick `SDL_GPU_TEXTURETYPE_CUBE`/`_2D_ARRAY`) and in
   `beginRenderPass` (`SDL3Device.cpp:678` — set the target `layer`/`mip_level`
   on `SDL_GPUColorTargetInfo`/`SDL_GPUDepthStencilTargetInfo`).
2. **True multi-light shadows:** replace the nearest-light scan
   (`Engine.cpp:2618`) with a budgeted set of casters indexed into a shadow
   *texture array* (needs step 1). Keep the per-face matrices from
   `pointShadowFaceMatrix`.
3. **Cascaded sun shadows:** split the single ortho frustum into 2–4 cascades
   (near/far), each its own `sunLightSpaceMatrix`. **The split math is pure.**
4. **Soft shadows:** widen the PCF kernel / add PCSS for the sun; multi-tap the
   point shadow (`samplePointShadow` is a single unfiltered tap today); add
   normal-offset bias. Shader work; the bias/kernel *offset* math is pure.
5. **Beyond forward-16:** clustered/tiled deferred to lift `kMaxForwardLights`.
   Large, GPU-bound; the cluster-assignment (froxel index from view-space pos)
   is pure and testable.

### Verifiable-here (PURE SEAMS — `ShadowMatrix.h` is header-only + tested)
`ShadowMatrix.h` is engine-independent and covered by `tests/test_shadow.cpp`
(sun matrix determinacy, up-vector degeneracy guard, all 6 cube faces —
`test_shadow.cpp:34–194`). New pure math that can land as sandbox PRs:
- **`cascadeSplitDistances(near, far, count, lambda)`** — the standard
  log/uniform blend cascade split scheme. Pure, add cases to `test_shadow.cpp`
  (monotonic, first==near, last==far, lambda blends between log and uniform).
- **`sunCascadeMatrix(sunDir, cascadeNear, cascadeFar, cameraFrustumCorners)`** —
  per-cascade ortho matrix fitted to a slice of the view frustum, reusing the
  existing `sunLightSpaceMatrix` machinery. Pure; testable (center projects to
  clip origin, corners stay in [-1,1], like the existing sun tests).
- **`normalOffsetBias(worldNormal, texelWorldSize, slope)`** — the peter-panning
  / acne bias offset. Pure vec math, easy to pin.
- **PCF kernel offset tables** (`vec2` taps for an N×N kernel) — pure, testable
  for symmetry/sum.
- **Clustered froxel index** (view-space position → cluster id) — pure integer
  math, testable.
These meaningfully shrink the bench: land the matrices/bias/kernel/cluster math
green in the sandbox, so the bench session is just shader-wiring + visual verify.

### Needs-bench
- The `TextureDesc`/`RenderPassDesc` backend changes (visual: a cube map renders
  correctly per face), multi-light shadow array binding, PCF/PCSS sampling look,
  the deferred/clustered lighting pass, and every soft-shadow visual judgement.

### Risks
- Cube/array `TextureDesc` + `RenderPassDesc` changes are **API-surface changes**
  touching every `createTexture`/`beginRenderPass` call — do them additively
  (defaults = current 2D/layer-0 behaviour) so the existing render is unchanged
  and only the new lighting paths opt in.
- Cascade fitting is notoriously easy to get subtly wrong (shimmer, seams);
  pin the split + fit math with tests before touching the GPU.

---

## Task 50 — CPack per-OS packaging

**Execution order: LAST.** PLAN: "depends on the cook tool + a validated render
so the artifact is worth shipping." No `install()`/CPack rules for the game exist
yet — root `CMakeLists.txt` only sets `ENABLE_INSTALL OFF` for submodules
(`:46`) and Catch2 install-off (`:136–137`); no `CPACK`/`BUNDLE`/`install(TARGETS
DoomScroller)` anywhere.

### The shipping layout to stage into (`ds::paths`, non-DS_DEV)
`engine/src/Paths.cpp` resolves everything **relative to the binary dir**
(`g_binDir`, set from `argv0` in `init` `:13`) in shipping mode:
- `assets()` → `g_binDir/assets` (`:25`)
- `shaders()` → `g_binDir/shaders` (`:34`) — **compiled bytecode**, not sources
- `shaderSources()` → `g_binDir/shaders` (`:42`)
- `userDir()` → `g_binDir/user` (`:50`)

So a package must place, next to the executable: `assets/` (cooked),
`shaders/` (compiled `.spv`/`.msl`/`.dxil` from `ds_compile_slang`'s
`_shader_out`, root `CMakeLists.txt:119`), and leave `user/` writable.

### File-level steps (root `CMakeLists.txt`, all CMake/CPack)
- `install(TARGETS DoomScroller RUNTIME DESTINATION .)` (or `bin` for the tarball).
- `install(DIRECTORY <cooked-assets> DESTINATION assets)` and the compiled
  shader out-dir → `DESTINATION shaders`, matching `Paths.cpp`'s
  binary-relative layout exactly.
- `include(CPack)` + generators: **Windows** `NSIS`/`WIX` (installer) + `ZIP`;
  **macOS** `Bundle`/`DragNDrop` — a `.app` needs an `Info.plist` and the Metal
  shaders (`.msl`) staged under `Contents/Resources`; note the `argv0`-relative
  `ds::paths` resolution must still land inside the bundle (may need a
  `MACOSX_BUNDLE`-aware path tweak). **Linux** `TGZ` tarball + optional AppImage
  (external tool, likely a CI script not pure CPack).
- macOS `.app` layout differs from the flat `g_binDir/assets` model — either set
  `MACOSX_BUNDLE_ICON_FILE`/resources so `argv0`'s parent still contains
  `assets/shaders`, or add a bundle branch to `Paths.cpp`.

### Verifiable-here
- **The CMake/CPack config itself builds/configures** in the sandbox: `install()`
  rules + `include(CPack)` can be added and `cmake --build --target package`
  *configures* (even if the staged binary isn't runnable). Config-time errors
  (missing DESTINATION, bad generator) surface without a GPU.
- A **pure test of the expected shipping layout** could assert `ds::paths`
  returns `binDir/assets`, `binDir/shaders`, `binDir/user` in non-DEV mode
  (guards against a layout drift breaking the package). `test_userstorage.cpp`
  already exercises `userDir`; extend similarly.

### Needs-bench
- A *validated* render + real cooked assets (task 48 + the cook tool) so the
  artifact is worth shipping; running the packaged binary on each OS; the macOS
  `.app` bundle path resolution; AppImage assembly.

### Risks
- The macOS bundle breaks the `argv0`-parent-relative `ds::paths` assumption —
  test the resolved paths inside a `.app` early.
- Shipping mode requires cooked assets (`PLAN` task 43/56); packaging before the
  cook seam is real means staging RGBA8-stub `.dstex` (still valid, just larger).

---

## Deferred Wave-D runtime wiring (tasks 51–54 glue)

The **pure cores already landed and are tested**:
`engine/include/engine/MetaProgression.h` (`startRun` `:88`, `applyRunResult`
`:79`, `applyAutoUnlocks` `:62`, `Unlock` bits `:29`; tested by
`test_metaprogression.cpp`) and `engine/include/engine/WeaponEconomy.h`
(`currencyForRun` `:55`, `purchase` `:200`, `modsForWeapon` `:220`,
`kEconCatalog` `:105`; tested by `test_economy.cpp`). What remains is
**Engine.cpp/UISystem call-site wiring only** — none of these functions are
*called* at runtime yet.

### Exact call sites to change
- **`persistSave` (`Engine.cpp:1259–1272`)** folds run stats inline. Swap the
  inline maxima for `m_save = applyRunResult(m_save, RunResult{wave,score,kills,
  bestCombo})` — identical fold, plus the auto-unlock rules. Then
  `writeEconomy(m_save, m_economy)` after crediting `currencyForRun(...)`.
- **`startGame` (`Engine.cpp:1179–1247`)** currently does `++m_save.totalRuns;`
  inline (`:1206`). Replace with `m_save = startRun(m_save);` (identical, but the
  tested path).
- **No `m_economy` field exists yet** in `Engine.h` — add `EconomyState m_economy`
  loaded via `readEconomy(m_save)` next to the existing save load
  (`Engine.cpp:243`).
- **Menu difficulty/level picker:** the Menu render (`Engine.cpp:2873–2915`,
  handled in `renderStateOverlay`) has no picker; input starts the game on
  Enter/click (`:1935–1937`, `:1966–1979`). Add UI + selection state; feed the
  chosen difficulty through the **SaveData 0-based ↔ wave.lua 1-based** mapping.
- **Difficulty mapping (the +1):** `SaveData.difficulty` is 0-based
  (`SaveData.h:101` — "0-based; wave.lua is 1-based"); the Lua side
  `ds.wave.set_difficulty(index)` is 1-based (`assets/scripts/wave.lua:60`,
  default 2 = Normal, `:33`). There is **no C++ trampoline** for it yet — add an
  `l_*` binding in `ScriptSystem.cpp` (documented per `CLAUDE.md`) or call it via
  the existing script table, and feed `m_save.difficulty + 1` when setting.
- **Shop / spend screen:** none exists (no shop/spend/purchase UI in
  `Engine.cpp`/`UISystem`). Build it on the intermission scaffold — the
  intermission banner is `Engine.cpp:3405–3412` and the current *automatic*
  upgrade grant is `grantWeaponUpgrade` (`Engine.cpp:1730–1751`, cycles by wave
  with no player choice). The shop replaces that auto-grant with `canPurchase`/
  `purchase` (`WeaponEconomy.h:192/200`) driven by UI, then
  `modsForWeapon(m_economy, slot)` feeds the firing systems.
- **Per-archetype mesh colors:** already wired — enemy albedo is archetype-tinted
  in `spawnEnemy` (`Engine.cpp:1760–1771`: Charger=orange, Ranged=blue,
  Grunt/default=red). No change needed unless new archetypes/hazards want new
  colors.
- **Hazard entities:** **not started** — no `HazardComponent` or hazard system
  anywhere (`Components.h`, `Engine.cpp`, `assets/scripts`). This is greenfield,
  not just wiring: add the component + a proximity/tick system (mirror
  `pickupSystem`), data-driven via Lua like archetypes.

### Verifiable-here
- The fold/economy cores are already tested; a sandbox PR can add tests pinning
  the *call-site equivalence* (e.g. that `applyRunResult` reproduces the current
  inline `persistSave` fold exactly — guards the swap) and the difficulty +1
  round-trip (`SaveData.difficulty` 0-based → Lua 1-based → back).
- A `HazardComponent` + pure hazard-tick math (damage-over-time / activation
  timing) can land header-only + tested before any rendering.

### Needs-bench
- Only the UI look/feel (menu picker, shop screen) needs eyes-on; the logic is
  sandbox-verifiable. Not strictly GPU-bound, but visual.

---

## Sandbox pre-work backlog (ordered by value / effort)

Land these as build+ctest-gated sandbox PRs **before** the bench session, to keep
shipping verifiable work and shrink each bench task to wiring+visual-verify:

1. **`debugDownloadTexture` format helpers (task 48, blocker).** Extract pure
   `bytesPerPixel(TextureFormat)` + `swizzleToRGB(fmt,...)`; unit-test against
   synthetic RGBA8/BGRA8/R32F rows. *Highest value:* unblocks every golden
   capture, and the biggest correctness bug in the capture path is now
   sandbox-verified. Low effort.
2. **Particle one-frame-lead parity test + shader fix (task 58).** Add a
   `test_particles.cpp` case asserting compute-fed instance positions equal the
   CPU `buildInstances()` output for the same pool+dt; drop the `+ vel*dt`
   extrapolation from `particle_sim.slang` (slangc-verified). High value (fixes a
   named Phase-3 caveat), low effort, pure + shader-compile.
3. **Cascade / soft-shadow math in `ShadowMatrix.h` (task 59).**
   `cascadeSplitDistances`, `sunCascadeMatrix`, `normalOffsetBias`, PCF kernel
   tables, clustered froxel index — all pure, all extend `test_shadow.cpp`. High
   value (task 59 is the biggest bench item; this carves out its testable core),
   medium effort.
4. **`selectTier`/caps mapping extension + `capsFromRawQuery` normalizer
   (task 55).** Tighten the tier rule (feature bit *and* VRAM), add
   `test_quality.cpp` cases; add a pure VRAM/feature-bit sanitizer. Medium value,
   low effort — moves the only testable slice of the device query off the bench.
5. **Wave-D call-site-equivalence + difficulty-mapping tests (Wave D).** Pin that
   `applyRunResult`/`startRun` reproduce the current inline `persistSave`/
   `startGame` folds, and the 0↔1-based difficulty round-trip. Low effort;
   de-risks the Engine.cpp swaps. Also: `HazardComponent` + pure hazard-tick math.
6. **`useMeshShaders(profile,caps)` gate predicate (task 57)** + author & compile
   `mesh_ms.slang` (add to `ds_compile_slang` SOURCES). Low value until the bench,
   but the shader-compile + gate test are free wins.
7. **Material-index packing helper (task 58 bindless).** Pure instance→material
   table + test; low effort, sequenced with task 59's array textures.
8. **Shipping-layout path assertions + CPack config (task 50).** Assert
   non-DEV `ds::paths` layout; add `install()`+`include(CPack)` so config-time
   packaging errors surface headless. Low effort, lowest value (needs the
   validated render to actually ship).
