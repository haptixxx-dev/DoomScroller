#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include "engine/CombatFeedback.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

using namespace ds;
using Catch::Approx;

TEST_CASE("addDamageEvent appends a fresh event", "[combat]") {
    std::vector<DamageEvent> events;
    addDamageEvent(events, {1.f, 2.f, 3.f}, 25, false);
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].amount == 25);
    REQUIRE(events[0].killed == false);
    REQUIRE(events[0].age == Approx(0.f));
    REQUIRE(events[0].worldPos == glm::vec3(1.f, 2.f, 3.f));

    addDamageEvent(events, {0.f, 0.f, 0.f}, 50, true);
    REQUIRE(events.size() == 2u);
    REQUIRE(events[1].killed == true);
    REQUIRE(events[1].amount == 50);
}

TEST_CASE("tickDamageEvents ages events and culls after lifetime", "[combat]") {
    std::vector<DamageEvent> events;
    addDamageEvent(events, {0.f, 0.f, 0.f}, 10, false); // lifetime 1.0
    REQUIRE(events.size() == 1u);

    tickDamageEvents(events, 0.5f);
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].age == Approx(0.5f));

    // Still alive just before lifetime.
    tickDamageEvents(events, 0.4f);
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].age == Approx(0.9f));

    // Crossing lifetime culls the event.
    tickDamageEvents(events, 0.2f);
    REQUIRE(events.empty());
}

TEST_CASE("worldToScreen maps a point in front near screen center", "[combat]") {
    // Camera at +z looking at the origin; aim at origin which sits in front.
    glm::mat4 view = glm::lookAt(glm::vec3(0.f, 0.f, 5.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    glm::mat4 proj = glm::perspective(glm::radians(60.f), 16.f / 9.f, 0.1f, 100.f);
    glm::mat4 vp   = proj * view;

    ScreenProjection p = worldToScreen(vp, glm::vec3(0.f), 1920, 1080);
    REQUIRE(p.visible == true);
    REQUIRE(p.screenPos.x == Approx(960.f).margin(1.f));
    REQUIRE(p.screenPos.y == Approx(540.f).margin(1.f));
}

TEST_CASE("worldToScreen reports a point behind the camera as not visible", "[combat]") {
    glm::mat4 view = glm::lookAt(glm::vec3(0.f, 0.f, 5.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    glm::mat4 proj = glm::perspective(glm::radians(60.f), 16.f / 9.f, 0.1f, 100.f);
    glm::mat4 vp   = proj * view;

    // Behind the camera (further along +z than the eye) => clip w <= 0.
    ScreenProjection p = worldToScreen(vp, glm::vec3(0.f, 0.f, 10.f), 1920, 1080);
    REQUIRE(p.visible == false);
}

TEST_CASE("worldToScreen pixel mapping uses a top-left origin", "[combat]") {
    glm::mat4 view = glm::lookAt(glm::vec3(0.f, 0.f, 5.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    glm::mat4 proj = glm::perspective(glm::radians(60.f), 16.f / 9.f, 0.1f, 100.f);
    glm::mat4 vp   = proj * view;

    ScreenProjection center = worldToScreen(vp, glm::vec3(0.f), 1920, 1080);
    ScreenProjection above  = worldToScreen(vp, glm::vec3(0.f, 1.f, 0.f), 1920, 1080);
    REQUIRE(center.visible == true);
    REQUIRE(above.visible == true);
    // A point higher in the world maps to a smaller y (top-left origin).
    REQUIRE(above.screenPos.y < center.screenPos.y);
}

TEST_CASE("triggerHitMarker and tickHitMarker drive a decaying flash", "[combat]") {
    HitMarker marker;
    REQUIRE(marker.timer == Approx(0.f));
    REQUIRE(marker.kill == false);

    triggerHitMarker(marker, true);
    REQUIRE(marker.timer > 0.f);
    REQUIRE(marker.kill == true);

    tickHitMarker(marker, 0.05f);
    REQUIRE(marker.timer > 0.f);
    REQUIRE(marker.kill == true);

    // Decaying past the flash clears the timer and the kill flag.
    tickHitMarker(marker, 1.f);
    REQUIRE(marker.timer == Approx(0.f));
    REQUIRE(marker.kill == false);
}
