# Bench-agent handoff prompt (Phase 4 GPU validation)

Paste everything below the line into an LLM coding agent running on the machine
with a real GPU + display. It is self-contained.

---

You are working on **DoomScroller**, a C++23 custom-engine FPS, on a machine that
HAS a real GPU + display (unlike the sandbox where this code was written). Your
job is the **Phase 4 bench-validation pass**: the engine's render features were
built and unit/CI-verified but have NEVER been seen running. You run them, judge
them by eye, capture reference images, and fix what's actually broken.

## Read first
- `PLAN.md` — full roadmap. Phase 4 = tasks 46–59.
- `docs/phase4-bench-plan.md` — THE authoritative file-level plan for every bench
  task, with exact `file:line` anchors and "what correct looks like" per feature.
  Follow it; this prompt is the cover sheet, that doc is the map.
- `CLAUDE.md` — build/test/CI conventions. Non-negotiable ones repeated below.

## Ground rules (same as the sandbox followed)
- **namespace ds; 4-space indent.** Engine talks only to the `rhi::` interface
  (sole exception: `ShaderLoader` via `nativeDevice()`).
- **Build/test:** `cmake --preset release` → `cmake --build build/release` →
  `ctest --test-dir build/release --output-on-failure`. Keep all suites green.
- **clang-format is version-exact:** CI uses **18.1.3**. Format only with
  `pipx run --spec "clang-format==18.1.3" clang-format -i <files>` (a different
  minor reflows differently and CI rejects it). Run it LAST, then rebuild+retest.
- **clang-tidy** `--warnings-as-errors='*'` over `engine/include engine/src`
  (minus `rhi/sdl3`). Watch `bugprone-narrowing-conversions` (static_cast) +
  `readability-qualified-auto`.
- **New shaders** must be added to `ds_compile_slang` SOURCES in the root
  `CMakeLists.txt` or they don't compile/ship.
- **Work on a branch, PR into `main`, CI must be green** (Win MSVC / macOS Metal
  / Linux Vulkan + format + tidy). One PR per task; keep diffs coherent.
- You have what the sandbox lacked: **you can RUN the binary.** Use it — every
  render claim must be backed by actually looking at output, not just a build.

## Task 0 — sanity: does it even run?
`cmake --preset release && cmake --build build/release` then launch
`build/release/bin/DoomScroller`. Confirm it opens a window and renders the arena
without validation errors / crashes on your backend. Note the backend
(Vulkan/D3D12/Metal) from the log. If it crashes, that's finding #1 — fix before
proceeding.

## Task 48 — capture golden reference images (do this FIRST; everything keys off it)
A capture harness already exists:
```
build/release/bin/DoomScroller --capture <outputDir>
```
It renders each fixed scene to `<outputDir>/golden_<scene>_<backend>.ppm` and
exits (no game loop). Today there is ONE scene: `startup`. The PPM format is P6,
byte-compatible with `engine/GoldenImage.h` (`readPpm`/`compareImages`).

Steps:
1. Run `--capture` on your backend. Confirm a `.ppm` is written and OPEN IT in an
   image viewer.
2. **Eyeball it against `docs/phase4-bench-plan.md` "The 5 unseen Phase-3
   features — what correct looks like."** Do NOT trust the image until a human
   (you, looking) confirms it. The sandbox literally could not render it.
3. If it looks correct → commit the PPM(s) as the task-47 golden references
   (put them where the bench-plan's task-47 section says; add a committed
   reference dir). These become regression baselines: `compareImages` diffs
   future captures against them with a per-pixel tolerance.
4. If it looks WRONG → that's a real Phase-3 bug the sandbox couldn't catch. Fix
   it (see Task-48 truth-test list below), re-capture, then commit the good ref.

### The 5 features to truth-test (fix what's broken; details in the bench-plan)
1. **HDR / ACES tonemap** — highlights roll off, no harsh clipping; overall
   brightness sane (not washed out / not crushed).
2. **PBR metallic-roughness** — metals vs dielectrics read differently; specular
   highlights track the light; no energy explosion.
3. **Sun shadow map** — shadow is on the correct side, attached to the caster
   (no peter-panning / no acne shimmer); orientation matches the sun direction.
4. **Bloom** — glows only on pixels above the bright threshold (muzzle/plasma/
   explosions), not the whole frame.
5. **Enhanced-tier compute particles** — if your GPU selects the Enhanced tier,
   particles simulate on the GPU compute path and match the CPU fallback (the
   one-frame-lead bug was already fixed sandbox-side; confirm no visible drift).

### Known capture caveats (from the sandbox authors, verify on hardware)
- `SDL3Device::debugDownloadTexture` was fixed to take the SOURCE texture format
  (was hardcoded 4B/px + swapchain-swizzle). Confirm the readback isn't
  mis-swizzled (colors correct, not BGR-swapped) on your backend.
- The captured frame is the post-tonemap LDR target at t=0 (deterministic). If
  `beginFrame()` fails (no swapchain), capture would dump an un-rendered target —
  watch for that.

## Task 55 — real device caps (make the tier real)
The device query is currently CONSERVATIVE: SDL 3.4.10 exposes no VRAM/mesh/
bindless query, so `queryCaps()` (in `engine/src/rhi/sdl3/SDL3Device.cpp`) returns
VRAM=0 / meshShaders=false / bindless=false → every device picks the **Minimum**
tier. On your real GPU:
- Log what `SDL_GetGPUDeviceProperties` actually returns on your backend.
- If SDL still exposes no usable VRAM/feature query, the conservative default is
  correct — document that Enhanced tier needs the raw-Vulkan backend (see Task
  57 below). Do NOT re-add a shader-format heuristic that over-promises.
- If you CAN reach a real query on your platform, populate the three fields; the
  pure `capsFromRawQuery` sanitizer + `selectTier` + `useMeshShaders` already
  handle the rest and are unit-tested. Confirm the tier the machine picks.

## Task 59 — the lighting overhaul (GPU half)
The sandbox landed the RHI CAPABILITY (cube/array textures via `TextureType` on
`TextureDesc`, `layer`/`mipLevel` on attachments — `SDL3Device` honors them) and
the PURE MATH (`engine/ShadowMatrix.h` cascade splits / `sunCascadeMatrix` /
`normalOffsetBias` / PCF kernels; `engine/ClusterGrid.h` froxel indexing — all
unit-tested). The GPU wiring is UNBUILT and yours to do + see:
1. **True multi-light shadows** — replace the nearest-light-only scan
   (`Engine.cpp` ~2618) with cube-map shadow targets (now that cube textures
   exist) for a budget of lights indexed into a texture array.
2. **Cascaded sun shadows** — use `cascadeSplitDistances` + `sunCascadeMatrix`
   (already tested) to render 2–4 cascades; verify no shimmer/seams by eye.
3. **Soft shadows** — wire the PCF kernels + `normalOffsetBias` into the shadow
   sample; PCSS optional. Judge acne/peter-panning visually.
4. **Clustered lighting** (optional, lifts `kMaxForwardLights=16`) — use
   `ClusterGrid.h` for froxel assignment.
Each sub-step: build green, RUN, eyeball, capture a new golden, commit.

## Task 57 + 58-bindless — BLOCKED (read before attempting)
Header-verified against pinned SDL `release-3.4.10`: SDL3-GPU has NO mesh/task
shader stage, NO bindless/descriptor-indexing API, and `nativeDevice()` returns
the opaque `SDL_GPUDevice*` with NO accessor for the raw `VkDevice`/
`VkPhysicalDevice`/`VkCommandBuffer`. So the mesh-shader path is UNREACHABLE
without a second, parallel **raw-Vulkan RHI backend** (its own `VkDevice` +
command buffers behind the `rhi::` interface). That is a large project — scope +
estimate it before writing code, and only if the mesh-shader payoff justifies
re-opening the RHI design. Do not fake it. See PLAN.md task-57 BLOCKED note.

## Wave-D runtime wiring (gameplay glue, no GPU risk but needs the running game)
`docs/phase4-bench-plan.md` "Deferred Wave-D runtime wiring" lists exact call
sites. The pure cores are landed + tested (`MetaProgression.h`, `WeaponEconomy.h`);
wire them into `Engine.cpp`/UISystem: menu level+difficulty picker, shop/spend
screen, per-archetype enemy colors, hazard entities, and the
`applyRunResult`/`startRun` call-site swaps. **Note:** swapping `persistSave` to
`applyRunResult` also FIXES a latent highScore-regression bug (documented in
`tests/test_metaprogression.cpp`) — do it.

## Working method (what the sandbox proved out — reuse it)
- One PR per task, green CI before merge (branch protection may require an
  approval — coordinate with the human).
- After each render change: build → **RUN and look** → capture → commit golden.
- For risky pure math, add/extend Catch2 tests (`tests/`, register in
  `tests/CMakeLists.txt`).
- Report findings honestly: if a feature looks wrong, say so with a screenshot,
  don't paper over it. The whole point of this pass is that a human finally SEES
  the render.

## Deliverables
1. Committed golden reference PPMs for every scene × backend you can run.
2. Fixes (as merged PRs) for anything the truth-test shows broken in the 5
   features.
3. Task 59 GPU lighting wired + visually verified, or a clear report of how far
   you got + what remains.
4. A short written verdict per feature: correct / fixed / still-broken, with
   evidence (screenshots).
