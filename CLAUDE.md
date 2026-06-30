# DoomScroller — agent notes

Fast 3D arena FPS (Doom / ULTRAKILL style). C++23, custom engine. Full design +
roadmap in `PLAN.md`; this file is the operational cheat-sheet.

## Build / test

```sh
cmake --preset debug          # configure (also: release, profile)
cmake --build build/debug     # build
ctest --test-dir build/debug --output-on-failure   # tests (Catch2)
```

Presets: `debug` (DS_DEV on), `release` (RelWithDebInfo), `profile` (Tracy).
The engine needs a GPU + window — it does **not** run headless in CI / sandboxes.
Verification ceiling here = build green + ctest pass; don't try to run the binary.

## Layout

- `engine/` — static lib `engine`. Talks **only** to the `rhi::` interface, never
  raw SDL3 GPU (sole exception: `ShaderLoader` via `nativeDevice()`). New `.cpp`
  goes in `engine/CMakeLists.txt`.
- `game/` — `DoomScroller` executable (thin `main.cpp` → `Engine`).
- `tools/` — offline tools (e.g. `level_convert`). Each new exe that links
  `engine` pulls in Jolt — see MSVC runtime note below.
- `shaders/` — Slang (`.slang`). **New shaders must be added to `ds_compile_slang`
  SOURCES in the root `CMakeLists.txt`** or they won't compile/ship.
- `tests/` — Catch2. Pure-logic tests link `engine_math` / `engine_headers` only
  (no SDL3/Jolt/mimalloc global state). Register in `tests/CMakeLists.txt`.

## CI / toolchain gotchas (the "what the hell is happening")

CI = `.github/workflows/ci.yml`: build matrix (Win MSVC / macOS Metal / Linux
Vulkan) + `clang-format` + `clang-tidy`, all gating.

1. **clang-format version skew.** CI installs `clang-format` via apt on
   `ubuntu-latest`, so the version tracks the runner image (currently **18.x**).
   Different major versions reflow code differently — a newer local
   `clang-format` will "fix" things CI then rejects, and vice-versa. Match CI's
   version before formatting. Without a system v18 you can run it isolated:
   ```sh
   pipx run --spec "clang-format==18.1.8" clang-format -i <files>
   ```
   CI check is `find engine game tests \( -name '*.cpp' -o -name '*.h' \)
   ! -path '*/extern/*' | xargs clang-format --dry-run --Werror`.

2. **clang-tidy is `--warnings-as-errors='*'`** over `engine/include engine/src`
   (minus `rhi/sdl3`), config in `.clang-tidy`. Common trip-ups: `int`→`float`
   narrowing (`bugprone-narrowing-conversions` → add explicit `static_cast<float>`)
   and assign-in-ctor-body (`cppcoreguidelines-prefer-member-initializer` → use
   the init list). Run with the **system** clang-tidy that matches your libstdc++;
   a pip-wheel clang-tidy can't parse a newer GCC's headers and floods false
   `clang-diagnostic-error`s from `/usr/include/c++` — that's a toolchain
   mismatch, not your code.

3. **Windows MSVC runtime (`LNK2038`).** Jolt defaults
   `USE_STATIC_MSVC_RUNTIME_LIBRARY=ON` (static `/MT`); everything else
   (SDL3, mimalloc, our exes) uses the dynamic `/MD` default → link mismatch on
   every Jolt-linked executable. Fixed in root `CMakeLists.txt` by forcing
   `USE_STATIC_MSVC_RUNTIME_LIBRARY OFF` **before** Jolt's `add_subdirectory`.
   Keep all targets on one runtime; if you add a static-lib submodule, check its
   MSVC-runtime default.

## Conventions

- `namespace ds`. 4-space indent. Match surrounding style exactly (clang-format
  enforces). Leave the build green and ctest passing before pushing.
- Submodules are version-pinned in `extern/` (no package registry).
- Work on a branch; PR into `main`. CI must be green to merge.
