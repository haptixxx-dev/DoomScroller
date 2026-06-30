#pragma once

#include <cmath>
#include <glm/glm.hpp>

namespace ds {

// Pure (engine-independent) CPU reference for a physically-based Cook-Torrance
// BRDF using the metallic-roughness workflow. This mirrors the GGX direct-light
// shading the `mesh.slang` fragment shader implements on the GPU; keeping a CPU
// copy lets us unit-test the math against engine_headers only (no SDL3 / Jolt /
// EnTT) and validate the shader against known reference values.
//
// All direction vectors (N, V, L, H) are assumed normalized by the caller.
// NdotX terms are clamped to >= 0 either by the caller or internally where the
// math would otherwise produce energy from below the surface.

// pi as a single-precision constant, matching the value used by the shader.
inline constexpr float kPi = 3.14159265358979323846f;

// Trowbridge-Reitz (GGX) normal distribution function. Returns the relative
// surface area of microfacets aligned with the half-vector H, given the cosine
// of the angle between the macro normal and H (`NdotH`) and a perceptual
// `roughness` in [0,1]. Uses a = roughness^2 (the standard remapping):
//   D = a^2 / (pi * ((NdotH^2 * (a^2 - 1) + 1))^2)
inline float distributionGGX(float NdotH, float roughness) {
    const float a     = roughness * roughness;
    const float a2    = a * a;
    const float nh    = NdotH > 0.f ? NdotH : 0.f;
    const float denom = nh * nh * (a2 - 1.f) + 1.f;
    return a2 / (kPi * denom * denom);
}

// Smith geometry term for direct lighting: the product of the Schlick-GGX
// self-shadowing/masking factors for the view and light directions. Uses the
// direct-lighting roughness remap k = (roughness + 1)^2 / 8 and
//   G1(x) = x / (x * (1 - k) + k),  G = G1(NdotV) * G1(NdotL).
// Returns a value in [0,1].
inline float geometrySmith(float NdotV, float NdotL, float roughness) {
    const float r    = roughness + 1.f;
    const float k    = (r * r) / 8.f;
    const float nv   = NdotV > 0.f ? NdotV : 0.f;
    const float nl   = NdotL > 0.f ? NdotL : 0.f;
    const float ggxV = nv / (nv * (1.f - k) + k);
    const float ggxL = nl / (nl * (1.f - k) + k);
    return ggxV * ggxL;
}

// Fresnel-Schlick approximation: the fraction of light reflected (vs.
// refracted) at the surface for a given `cosTheta` (cosine of the angle between
// the view/half-vector and the normal) and base reflectance `F0`.
//   F = F0 + (1 - F0) * (1 - cosTheta)^5
inline glm::vec3 fresnelSchlick(float cosTheta, const glm::vec3& F0) {
    const float c  = 1.f - (cosTheta > 0.f ? cosTheta : 0.f);
    const float c5 = c * c * c * c * c;
    return F0 + (glm::vec3(1.f) - F0) * c5;
}

// Base reflectance F0 for the metallic-roughness workflow. Dielectrics use a
// fixed 0.04 reflectance; metals tint reflectance by their albedo. The
// `metallic` parameter in [0,1] linearly blends between the two.
inline glm::vec3 computeF0(const glm::vec3& albedo, float metallic) {
    return glm::mix(glm::vec3(0.04f), albedo, metallic);
}

// Full Cook-Torrance direct-light shading term for a single light. Combines the
// GGX distribution, Smith geometry and Fresnel terms into a specular lobe, adds
// a Lambertian diffuse lobe weighted by the (energy-conserving) non-metallic
// diffuse fraction, and scales the result by the incoming `radiance` and the
// cosine foreshortening NdotL.
//
// Returns vec3(0) when the light is at or below the surface (NdotL <= 0).
inline glm::vec3 cookTorrance(const glm::vec3& N, const glm::vec3& V, const glm::vec3& L, const glm::vec3& albedo,
                              float metallic, float roughness, const glm::vec3& radiance) {
    const float NdotL = glm::dot(N, L);
    if (NdotL <= 0.f)
        return glm::vec3(0.f);

    const glm::vec3 H     = glm::normalize(V + L);
    const float NdotV     = glm::max(glm::dot(N, V), 0.f);
    const float NdotH     = glm::max(glm::dot(N, H), 0.f);
    const float HdotV     = glm::max(glm::dot(H, V), 0.f);
    const float NdotLclmp = NdotL; // already > 0 here

    const glm::vec3 F0 = computeF0(albedo, metallic);
    const float D      = distributionGGX(NdotH, roughness);
    const float G      = geometrySmith(NdotV, NdotLclmp, roughness);
    const glm::vec3 F  = fresnelSchlick(HdotV, F0);

    const glm::vec3 numerator = D * G * F;
    const float denominator   = 4.f * NdotV * NdotLclmp + 1e-4f;
    const glm::vec3 specular  = numerator / denominator;

    // Energy conservation: refracted (diffuse) light is what is not reflected,
    // and metals have no diffuse component.
    const glm::vec3 kd      = (glm::vec3(1.f) - F) * (1.f - metallic);
    const glm::vec3 diffuse = kd * albedo / kPi;

    return (diffuse + specular) * radiance * NdotLclmp;
}

} // namespace ds
