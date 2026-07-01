# Task 48 bench-validation pass — verdict (2026-07-01)

Ran on real hardware: Linux, dual NVIDIA (RTX 2060 + RTX 5070), Vulkan backend
(`SDL_GPU Driver: Vulkan`, device picked = `NVIDIA GeForce RTX 5070`).

## Task 0 — sanity: does it run?

**Pass, with a caveat.** `cmake --preset release && cmake --build build/release`
is clean (811/812) and `ctest --test-dir build/release` is 5/5 green. But
launching `build/release/bin/DoomScroller` directly **crashes**:

```
[fatal] ShaderLoader: cannot open .../build/release/bin/shaders/mesh.vertex.spv
```

Root cause: `release`/`profile` presets don't define `DS_DEV`, so
`ds::paths::shaders()` (`engine/src/Paths.cpp:46`) resolves to
`resourceRoot()/shaders` — i.e. a directory expected **next to the binary**.
That layout is only staged by `cmake --install` (see the `install(DIRECTORY
${CMAKE_BINARY_DIR}/shaders/ ...)` rules in `game/CMakeLists.txt:65,72`); a
raw build-tree binary never gets it. Running `cmake --install build/release
--prefix <dir>` first, or using the `debug` preset (`DS_DEV=ON`, which reads
shaders straight from `DS_BINARY_DIR/shaders`), both work fine.

This isn't a Phase-3 render bug — it's a doc/workflow gap: Task 0's instructions
assume the raw build-tree binary is runnable, and for release/profile it isn't
without an extra install step. Not fixed here (out of scope for this pass per
user direction); flagging so the next bench session doesn't re-discover it.

Used `build/debug/bin/DoomScroller` for the actual run: window opens, renders
the arena + main menu, no crash, no visible Vulkan validation-layer errors
(validation layers aren't installed on this box — SDL logs "Validation layers
not found, continuing without validation" — so absence of errors isn't a
strong signal either way, just no *visible* corruption).

## Task 48 — golden capture

**Pass, for what the current scene covers.** `--capture` writes
`golden_startup_vulkan.ppm` (1280x720, 4bpp source, RGB PPM out) cleanly, no
crash, no swizzle bugs (colors correct, not BGR-swapped — text is white,
grays read as grays). Committed as `tests/golden/golden_startup_vulkan.ppm`.

**Caveat — this doesn't truth-test the 5 features.** The only scene
(`startup`) is the empty-arena main-menu backdrop: flat ambient-lit gray box,
no dynamic point lights, no metallic props, no bright emissives above the
bloom threshold, no particles spawned. So of the "5 unseen Phase-3 features":

| # | Feature | Verdict |
|---|---|---|
| 1 | HDR/ACES tonemap | **Not exercised.** No emissive >1.0 in this scene to test rolloff. What's visible (midtones) looks filmic-dark, not washed — consistent with correct, but not a real test. |
| 2 | PBR metallic/roughness | **Not exercised.** No metallic surfaces in the menu backdrop. |
| 3 | Sun shadow orientation/bias | **Inconclusive.** Diagonal lines on floor/walls in the capture are the tiled-texture grout pattern, not confirmed shadow geometry — can't tell caster-attachment or bias from this scene. |
| 4 | Bloom | **Not exercised.** Nothing crosses the bright threshold. |
| 5 | Compute particles | **Not exercised.** No particles spawn on the menu screen. |

Real truth-testing of 1–5 needs a gameplay scene (in the arena, lights on,
shooting/particles active) — the capture harness only has `startup` today.
That's a known gap per `docs/phase4-bench-plan.md` ("Today there is ONE
scene"), not something broken by this pass.

## Task 55 — real device caps

**Confirmed, no code change needed.** Raised the `gpu` SDL log category to
`info` (`SDL_LOGGING=gpu=info ./DoomScroller`) to see the existing
diagnostic line in `SDL3Device.cpp:95`:

```
[caps] backend=vulkan device="NVIDIA GeForce RTX 5070" driver="NVIDIA"
shaderFmts=0x2 (VRAM/meshShaders/bindless unqueryable via SDL3 GPU ->
conservative Minimum; device values UNVERIFIED until bench)
```

On real hardware, pinned SDL 3.4.10 genuinely exposes no VRAM/mesh/bindless
query even for a discrete RTX 5070 — the conservative Minimum-tier default in
`queryCaps()` is correct, exactly as `docs/phase4-bench-plan.md` predicted.
Per that doc's own instruction: do not re-add a shader-format heuristic here.

## Not attempted this pass

Task 59 (GPU lighting overhaul), Task 57/58 (blocked mesh-shader/bindless
investigation), and the Wave-D gameplay wiring were explicitly deferred —
each is a substantial multi-PR effort and out of scope for this findings-only
pass.
