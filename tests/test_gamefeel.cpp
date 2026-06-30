#include "engine/GameFeel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

using namespace ds;

TEST_CASE("addTrauma clamps into [0,1]", "[gamefeel]") {
    ScreenShake s;
    addTrauma(s, 2.0f); // over-add
    REQUIRE(s.trauma == Catch::Approx(1.0f));
    addTrauma(s, 0.5f); // already full -> stays clamped
    REQUIRE(s.trauma == Catch::Approx(1.0f));

    ScreenShake s2;
    addTrauma(s2, -1.0f); // negative floors at 0
    REQUIRE(s2.trauma == Catch::Approx(0.0f));
    addTrauma(s2, 0.3f);
    REQUIRE(s2.trauma == Catch::Approx(0.3f));
}

TEST_CASE("decayTrauma reduces and floors at zero", "[gamefeel]") {
    ScreenShake s;
    s.trauma = 1.0f;
    decayTrauma(s, 0.5f, 1.0f); // -0.5
    REQUIRE(s.trauma == Catch::Approx(0.5f));
    decayTrauma(s, 2.0f, 1.0f); // would go negative -> clamps to 0
    REQUIRE(s.trauma == Catch::Approx(0.0f));
}

TEST_CASE("shakeOffset with zero trauma is exactly zero", "[gamefeel]") {
    REQUIRE(shakeOffset(0.0f, 0.0f) == glm::vec3(0.0f));
    REQUIRE(shakeOffset(0.0f, 12.34f) == glm::vec3(0.0f));
    REQUIRE(shakeOffset(0.0f, -987.6f) == glm::vec3(0.0f));
}

TEST_CASE("shakeOffset grows with trauma", "[gamefeel]") {
    const float t = 3.7f;
    float full    = glm::length(shakeOffset(1.0f, t));
    float half    = glm::length(shakeOffset(0.5f, t));
    REQUIRE(full > half); // trauma^2 scaling -> 1.0 punches harder than 0.5
    REQUIRE(half > 0.0f); // 0.5 still shakes some
}

TEST_CASE("shakeOffset magnitude follows the trauma^2 law", "[gamefeel]") {
    // Same time -> identical noise vector, so the ratio of offset magnitudes is
    // purely the magnitude scalar trauma^2. A linear (trauma) impl would give a
    // 2x ratio here and fail; the documented squared law gives 4x.
    const float t = 3.7f;
    float full    = glm::length(shakeOffset(1.0f, t));
    float half    = glm::length(shakeOffset(0.5f, t));
    REQUIRE(half > 0.0f);
    REQUIRE(full / half == Catch::Approx(4.0f).epsilon(1e-3f)); // (1.0/0.5)^2
}

TEST_CASE("shakeOffset axes move independently", "[gamefeel]") {
    // Distinct per-axis seeds/frequencies must yield distinct components; a bug
    // that returned the same noise on all three axes would pass the in/out tests
    // but fail here.
    glm::vec3 o = shakeOffset(1.0f, 2.345f);
    REQUIRE(o.x != Catch::Approx(o.y));
    REQUIRE(o.y != Catch::Approx(o.z));
    REQUIRE(o.x != Catch::Approx(o.z));
}

TEST_CASE("shakeOffset is deterministic for the same inputs", "[gamefeel]") {
    glm::vec3 a = shakeOffset(0.8f, 5.5f);
    glm::vec3 b = shakeOffset(0.8f, 5.5f);
    REQUIRE(a == b);
}

TEST_CASE("shakeNoise stays within [-1,1]", "[gamefeel]") {
    for (int i = 0; i < 50; ++i) {
        float n = shakeNoise(static_cast<float>(i) * 0.731f);
        REQUIRE(n >= -1.0f);
        REQUIRE(n < 1.0f);
    }
}

TEST_CASE("recoil spring settles back toward zero", "[gamefeel]") {
    Recoil r;
    addRecoil(r, glm::vec2(0.2f, 0.3f));
    REQUIRE(glm::length(r.velocity) > 0.0f);

    // Integrate the spring for a while; offset should relax close to zero.
    const float dt = 1.f / 120.f;
    for (int i = 0; i < 400; ++i)
        tickRecoil(r, dt);

    REQUIRE(glm::length(r.offset) < 1e-3f);
    REQUIRE(glm::length(r.velocity) < 1e-3f);
}

TEST_CASE("addRecoil stacks impulses additively", "[gamefeel]") {
    Recoil r;
    addRecoil(r, glm::vec2(0.1f, 0.0f));
    addRecoil(r, glm::vec2(0.2f, 0.0f));
    REQUIRE(r.velocity.x == Catch::Approx(0.3f));
    REQUIRE(r.velocity.y == Catch::Approx(0.0f));
}

TEST_CASE("hitstop returns frozenScale while active then 1.0", "[gamefeel]") {
    Hitstop h;
    triggerHitstop(h, 0.1f);

    const float dt = 1.f / 60.f; // ~0.01667s
    // First few ticks are inside the 0.1s window -> frozen.
    REQUIRE(tickHitstop(h, dt, 0.05f) == Catch::Approx(0.05f));
    REQUIRE(tickHitstop(h, dt, 0.05f) == Catch::Approx(0.05f));

    // Drain the rest of the window.
    for (int i = 0; i < 8; ++i)
        tickHitstop(h, dt, 0.05f);
    REQUIRE(h.timer == Catch::Approx(0.0f));

    // Timer elapsed -> normal time scale.
    REQUIRE(tickHitstop(h, dt, 0.05f) == Catch::Approx(1.0f));
}

TEST_CASE("triggerHitstop never shortens a longer active hitstop", "[gamefeel]") {
    Hitstop h;
    triggerHitstop(h, 0.2f);
    triggerHitstop(h, 0.05f); // shorter -> keep the longer 0.2f
    REQUIRE(h.timer == Catch::Approx(0.2f));
    triggerHitstop(h, 0.3f);  // longer -> extend
    REQUIRE(h.timer == Catch::Approx(0.3f));
}
