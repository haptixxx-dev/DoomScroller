#include "engine/PbrBrdf.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/glm.hpp>

using Catch::Approx;
using namespace ds;

TEST_CASE("computeF0 blends dielectric 0.04 with albedo by metallic", "[pbr]") {
    const glm::vec3 albedo{0.8f, 0.2f, 0.1f};

    const glm::vec3 dielectric = computeF0(albedo, 0.f);
    REQUIRE(dielectric.x == Approx(0.04f));
    REQUIRE(dielectric.y == Approx(0.04f));
    REQUIRE(dielectric.z == Approx(0.04f));

    const glm::vec3 metal = computeF0(albedo, 1.f);
    REQUIRE(metal.x == Approx(albedo.x));
    REQUIRE(metal.y == Approx(albedo.y));
    REQUIRE(metal.z == Approx(albedo.z));
}

TEST_CASE("fresnelSchlick returns F0 at normal incidence and ~1 at grazing", "[pbr]") {
    const glm::vec3 F0{0.04f, 0.04f, 0.04f};

    const glm::vec3 head = fresnelSchlick(1.f, F0);
    REQUIRE(head.x == Approx(0.04f));
    REQUIRE(head.y == Approx(0.04f));
    REQUIRE(head.z == Approx(0.04f));

    const glm::vec3 grazing = fresnelSchlick(0.f, F0);
    REQUIRE(grazing.x == Approx(1.f));
    REQUIRE(grazing.y == Approx(1.f));
    REQUIRE(grazing.z == Approx(1.f));
}

TEST_CASE("distributionGGX is positive, finite and peaks toward the half-vector", "[pbr]") {
    const float roughness = 0.5f;

    const float atPeak = distributionGGX(1.f, roughness);
    const float atHalf = distributionGGX(0.5f, roughness);

    REQUIRE(atPeak > 0.f);
    REQUIRE(atHalf > 0.f);
    REQUIRE(std::isfinite(atPeak));
    REQUIRE(std::isfinite(atHalf));
    REQUIRE(atPeak > atHalf);
}

TEST_CASE("geometrySmith stays within [0,1]", "[pbr]") {
    const float g = geometrySmith(0.8f, 0.6f, 0.4f);
    REQUIRE(g >= 0.f);
    REQUIRE(g <= 1.f);

    const float grazing = geometrySmith(0.05f, 0.05f, 0.9f);
    REQUIRE(grazing >= 0.f);
    REQUIRE(grazing <= 1.f);
}

TEST_CASE("PBR terms pin their exact formula constants", "[pbr]") {
    // distributionGGX(NdotH=1, roughness=0.5): a = 0.25, a2 = 0.0625,
    // denom = (1*1*(a2-1)+1)^2 = (0.0625)^2 = 0.00390625,
    // D = a2 / (pi * denom) = 0.0625 / (pi * 0.00390625) ~= 5.0930.
    REQUIRE(distributionGGX(1.0f, 0.5f) == Approx(5.0930f).epsilon(1e-3f));

    // fresnelSchlick(cosTheta=0.5, F0=0.04) pins the ^5 exponent:
    // 0.04 + (1-0.04)*(1-0.5)^5 = 0.04 + 0.96*0.03125 = 0.04 + 0.03 = 0.07.
    REQUIRE(fresnelSchlick(0.5f, glm::vec3(0.04f)).x == Approx(0.07f).epsilon(1e-3f));

    // geometrySmith(NdotV=0.8, NdotL=0.6, roughness=0.4) pins k = (r+1)^2/8 and G1:
    //   k = (1.4)^2 / 8 = 1.96/8 = 0.245
    //   G1(0.8) = 0.8/(0.8*(1-0.245)+0.245) = 0.8/0.849 ~= 0.94229
    //   G1(0.6) = 0.6/(0.6*(1-0.245)+0.245) = 0.6/0.698 ~= 0.85960
    //   G = 0.94229 * 0.85960 ~= 0.81000
    REQUIRE(geometrySmith(0.8f, 0.6f, 0.4f) == Approx(0.8100f).epsilon(1e-2f));
}

TEST_CASE("cookTorrance returns zero when the light is below the surface", "[pbr]") {
    const glm::vec3 N{0.f, 0.f, 1.f};
    const glm::vec3 V{0.f, 0.f, 1.f};
    const glm::vec3 L{0.f, 0.f, -1.f}; // below the surface
    const glm::vec3 albedo{0.7f, 0.7f, 0.7f};
    const glm::vec3 radiance{1.f, 1.f, 1.f};

    const glm::vec3 c = cookTorrance(N, V, L, albedo, 0.f, 0.5f, radiance);
    REQUIRE(c.x == Approx(0.f));
    REQUIRE(c.y == Approx(0.f));
    REQUIRE(c.z == Approx(0.f));
}

TEST_CASE("cookTorrance gives a positive finite color for a lit rough surface", "[pbr]") {
    const glm::vec3 N{0.f, 0.f, 1.f};
    const glm::vec3 V{0.f, 0.f, 1.f};
    const glm::vec3 L{0.f, 0.f, 1.f}; // light directly above
    const glm::vec3 albedo{0.7f, 0.7f, 0.7f};
    const glm::vec3 radiance{1.f, 1.f, 1.f};

    const glm::vec3 c = cookTorrance(N, V, L, albedo, 0.f, 0.8f, radiance);
    REQUIRE(c.x > 0.f);
    REQUIRE(c.y > 0.f);
    REQUIRE(c.z > 0.f);
    REQUIRE(std::isfinite(c.x));
    REQUIRE(std::isfinite(c.y));
    REQUIRE(std::isfinite(c.z));
}
