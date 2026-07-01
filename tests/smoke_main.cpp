// Headless smoke target (PLAN task 46).
//
// A CI-runnable executable that links the FULL `engine` static library and
// constructs every device-independent engine subsystem WITHOUT creating a GPU
// device or window (it never calls rhi::createDevice / SDL_CreateWindow). The
// GPU-gated build cannot surface link errors, static-init-order faults, or
// PIMPL-ctor regressions in the sandbox/CI because it needs a display; this
// target does, by exercising the SDL3/Jolt/Lua/miniaudio/mimalloc-backed paths
// that only link into the full `engine` lib (the pure header/math systems are
// already covered by ds_engine_tests).
//
// It runs a handful of CPU-only "frames" (physics step, particle update, Lua
// event callbacks) and returns non-zero on any detectable failure so ctest
// treats a regression as a hard failure. AudioSystem::init() legitimately
// returns false on a headless CI runner (no audio device); per its documented
// contract the object stays usable as a silent no-op, so that is NOT a failure
// here — we only require that it constructs and tears down without crashing.

#include "engine/AudioSystem.h"
#include "engine/Camera.h"
#include "engine/GameFeel.h"
#include "engine/MovementTech.h"
#include "engine/ParticleSystem.h"
#include "engine/Paths.h"
#include "engine/PhysicsWorld.h"
#include "engine/ScriptSystem.h"
#include "engine/StyleMeter.h"
#include "engine/ecs/Components.h"

#include <cmath>
#include <cstdio>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "[smoke] FAIL: %s\n", what);
        ++g_failures;
    } else {
        std::fprintf(stderr, "[smoke] ok:   %s\n", what);
    }
}

// Physics: construct the Jolt-backed world, add the body kinds the game uses,
// step it, and read state back. Exercises the PIMPL ctor + Jolt global init.
void smokePhysics() {
    ds::PhysicsWorld physics;
    physics.init();

    physics.addStaticBox({0.f, -1.f, 0.f}, {20.f, 1.f, 20.f});
    uint32_t player = physics.addCapsule(0.9f, 0.3f, {0.f, 2.f, 0.f});
    uint32_t box    = physics.addDynamicBox({0.f, 5.f, 0.f}, {0.5f, 0.5f, 0.5f}, {0.f, -1.f, 0.f});

    for (int i = 0; i < 8; ++i)
        physics.step(1.f / 60.f);

    glm::vec3 p = physics.getPosition(player);
    check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),
          "physics: player position finite after stepping");

    // A ray straight down from the player should hit the floor box.
    bool grounded = physics.castRayDown({0.f, 2.f, 0.f}, 100.f, player);
    check(grounded, "physics: downward ray hits the static floor");

    physics.removeBody(box);
    physics.removeBody(player);
}

// Audio: construct + init + a couple of no-op-safe calls + shutdown. On a
// headless runner init() returns false; the object must stay usable (silent).
void smokeAudio() {
    ds::AudioSystem audio;
    bool ok = audio.init(); // false on headless CI is expected and fine
    // initialized() must agree with init()'s return — the real contract. On a
    // headless runner both are false (silent no-op); on a real device both are
    // true. A regression that desynced them (e.g. init() lying) fails here.
    check(audio.initialized() == ok, "audio: initialized() agrees with init() return");
    audio.setMasterVolume(0.5f);
    audio.play("sfx/does_not_exist.wav"); // safe no-op on missing file
    audio.setListener({0.f, 0.f, 0.f}, {0.f, 0.f, -1.f});
    audio.shutdown();
    check(audio.initialized() == false, "audio: not initialised after shutdown");
}

// Scripting: bring up the Lua VM, load the real shipped gameplay scripts from
// the asset tree, and fire the event callbacks the engine drives each wave.
void smokeScripts() {
    ds::ScriptSystem scripts;
    bool inited = scripts.init(); // default callbacks
    check(inited, "scripts: lua_State initialised");
    if (!inited)
        return;

    // Load every shipped gameplay script; a missing/broken file leaves its
    // module undefined but must not crash. This catches Lua syntax regressions
    // in the actual assets the game ships. Scripts are located via the baked
    // DS_ASSETS_DIR (the source-tree assets/), NOT ds::paths::assets(): CI runs
    // the release preset (DS_DEV OFF), where paths::assets() resolves to a
    // binary-relative dir that has no cooked assets staged next to ds_smoke —
    // using it here would fail every load spuriously. Same rationale as the
    // DS_ASSETS_DIR injection in ds_script_tests (tests/CMakeLists.txt).
    static constexpr const char* kScripts[] = {
        "scripts/enemy_ai.lua", "scripts/wave.lua",    "scripts/boss.lua",
        "scripts/parry.lua",    "scripts/pickups.lua", "scripts/hooks.lua",
    };
    for (const char* rel : kScripts) {
        std::string path = std::string(DS_ASSETS_DIR) + "/" + rel;
        bool loaded      = scripts.loadFile(path);
        check(loaded, rel);
    }

    // Fire the wave-start hook a few times; must not throw/crash.
    for (int wave = 1; wave <= 3; ++wave)
        scripts.onWaveStart(wave);
    check(scripts.initialized(), "scripts: still initialised after callbacks");
    scripts.shutdown();
}

// Particles: pooled CPU sim. Emit each preset, integrate, confirm the alive
// count stays within the fixed pool.
void smokeParticles() {
    ds::ParticleSystem particles;
    particles.emit(ds::ParticleSystem::Effect::MuzzleFlash, {0.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, 16);
    particles.emit(ds::ParticleSystem::Effect::BloodBurst, {1.f, 1.f, 1.f}, {0.f, 1.f, 0.f}, 24);
    particles.emit(ds::ParticleSystem::Effect::ImpactSparks, {-1.f, 0.f, 2.f}, {1.f, 0.f, 0.f}, 24);
    particles.emit(ds::ParticleSystem::Effect::Explosion, {0.f, 2.f, 0.f}, {0.f, 1.f, 0.f}, 64);
    // Lower bound: emit() must actually populate the pool. Without this a
    // no-op'd emitter (aliveCount stays 0) would pass the upper-bound check
    // below as a false green.
    check(particles.aliveCount() > 0, "particles: emit populated the pool");
    for (int i = 0; i < 30; ++i)
        particles.update(1.f / 60.f);
    check(particles.aliveCount() <= ds::ParticleSystem::kMaxParticles, "particles: alive count within pool capacity");
}

// ECS + pure gameplay math: build a small world of entities with the real
// components and tick the header-only tech so a full-engine link proves it all
// composes (a belt-and-braces echo of ds_engine_tests through the real lib).
void smokeEcsAndMath() {
    entt::registry world;
    for (int i = 0; i < 32; ++i) {
        entt::entity e = world.create();
        world.emplace<ds::Transform>(e, ds::Transform{glm::vec3(static_cast<float>(i), 0.f, 0.f), {}, glm::vec3(1.f)});
        world.emplace<ds::HealthComponent>(e, ds::HealthComponent{100, 100});
    }
    std::size_t count = 0;
    world.view<ds::Transform, ds::HealthComponent>().each([&](auto, auto&, auto&) { ++count; });
    check(count == 32, "ecs: registry iterated all created entities");

    ds::Camera cam;
    glm::mat4 vp = cam.projMatrix(16.f / 9.f) * cam.viewMatrix();
    check(std::isfinite(vp[3][3]) || vp[3][3] == 0.f, "camera: view-projection built");

    ds::ScreenShake shake;
    ds::addTrauma(shake, 1.f); // clamps to 1
    for (int i = 0; i < 10; ++i)
        ds::decayTrauma(shake, 1.f / 60.f);
    // Trauma must have decayed from the clamped 1.0 and stayed finite/in-range.
    check(std::isfinite(shake.trauma) && shake.trauma >= 0.f && shake.trauma < 1.f,
          "gamefeel: trauma decayed from 1.0 and stayed in [0,1)");

    ds::StyleState style;
    ds::StyleConfig styleCfg;
    for (int i = 0; i < 5; ++i)
        ds::tickStyle(style, 1.f / 60.f, styleCfg);
    // With no style events the meter decays toward 0 and must never go negative.
    check(std::isfinite(style.points) && style.points >= 0.f, "style: points finite and non-negative after decay");
}

} // namespace

int main(int argc, char** argv) {
    ds::paths::init(argc > 0 ? argv[0] : "smoke");
    std::fprintf(stderr, "[smoke] DoomScroller headless smoke target\n");

    smokePhysics();
    smokeAudio();
    smokeScripts();
    smokeParticles();
    smokeEcsAndMath();

    if (g_failures != 0) {
        std::fprintf(stderr, "[smoke] %d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::fprintf(stderr, "[smoke] all checks passed\n");
    return 0;
}
