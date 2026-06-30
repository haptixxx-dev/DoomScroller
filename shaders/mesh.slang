struct VSInput {
    float3 pos    : POSITION0;
    float3 color  : COLOR0;
    float2 uv     : TEXCOORD0;
    float3 normal : NORMAL0;
    // Per-instance model matrix (task 28): four rows of a column-major float4x4
    // streamed from the instanced vertex binding (slot 1, locations 4..7). The
    // CPU writes glm::mat4 (column-major) as 4 consecutive float4 columns, so
    // m0..m3 are the matrix's COLUMNS; we reassemble with them as columns below.
    float4 m0 : TEXCOORD2;
    float4 m1 : TEXCOORD3;
    float4 m2 : TEXCOORD4;
    float4 m3 : TEXCOORD5;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 color    : COLOR0;
    float2 uv       : TEXCOORD0;
    float3 normalWS : NORMAL0;
    float3 posWS    : TEXCOORD1;
};

// Vertex uniform: just the shared view-projection now (task 28). The per-object
// model matrix is the instanced vertex stream above; mvp = viewProj * model is
// computed in the shader rather than pushed per draw.
struct PushConstants {
    column_major float4x4 viewProj;
};

// Forward point lights. Layout matches ds::LightBuffer (std140): a 16-byte
// header (count + padding) followed by up to 16 lights, each two float4s.
static const int kMaxLights = 16;

struct Light {
    float4 posRadius;      // xyz = world position, w = radius
    float4 colorIntensity; // rgb = color,          w = intensity
};

struct LightData {
    int4 count; // .x = active light count
    Light lights[kMaxLights];
};

// Vertex uniform slot 0. SDL3 GPU Metal binds vertex uniform buffers at
// buffer(0..N) and vertex INPUT buffers at buffer(14+), so push constants
// MUST be MSL buffer(0). SPIRV set=1 binding=0 (via vk::binding), DXIL b0.
[vk::binding(0, 1)]
ConstantBuffer<PushConstants> gPush : register(b0);

// Fragment texture+sampler slot 0: SPIRV set=2 binding=0 as combined image sampler.
// Sampler2D emits OpTypeSampledImage, which matches SDL_GPU's COMBINED_IMAGE_SAMPLER layout.
[vk::binding(0, 2)]
Sampler2D gAlbedo;

// Fragment uniform slot 0: SDL3 GPU maps fragment uniform buffers to SPIRV
// set=3 (MSL buffer(0)). Filled via pushFragmentConstants(slot 0) each frame.
[vk::binding(0, 3)]
ConstantBuffer<LightData> gLights : register(b0, space3);

// Per-entity PBR material. Fragment uniform slot 1 (SPIRV set=3 binding=1,
// MSL buffer(1)). Pushed via pushFragmentConstants(slot 1) per draw. Mirrors
// ds::MaterialBuffer (std140). cameraPos rides along for the BRDF view vector.
// TODO: optional metallic-roughness / normal map textures could extend this.
struct MaterialData {
    float metallic;
    float roughness;
    float pad0;
    float pad1;
    float3 cameraPos;
    float pad2;
    float3 baseColorTint;
    float pad3;
};

[vk::binding(1, 3)]
ConstantBuffer<MaterialData> gMaterial : register(b1, space3);

// Single-cascade directional shadow map (task 31). The depth-only shadow pass
// renders the scene from the sun's POV into this texture; here we project the
// fragment's world position into light space and compare depths to decide if
// it is in shadow. Fragment texture+sampler slot 1 (SPIRV set=2 binding=1,
// MSL texture(1)/sampler(1)). The mesh FS therefore declares 2 samplers.
[vk::binding(1, 2)]
Texture2D<float> gShadowMap : register(t1, space2);

[vk::binding(1, 2)]
SamplerState gShadowSampler : register(s1, space2);

// Light-space view-projection matrix (ds::sunLightSpaceMatrix on the CPU).
// Fragment uniform slot 2 (SPIRV set=3 binding=2, MSL buffer(2)).
struct ShadowData {
    column_major float4x4 lightSpace;
    int4 params; // .x = shadow map resolution (texels per side)
};

[vk::binding(2, 3)]
ConstantBuffer<ShadowData> gShadow : register(b2, space3);

// Point-light shadow (distance-based, 6 perspective faces — see
// shaders/point_shadow_depth.slang and engine/ShadowMatrix.h's
// pointShadowFaceMatrix). Exactly ONE point light shadow-casts per frame
// (whichever is nearest the camera, chosen CPU-side); every other point
// light stays unshadowed exactly as before. Each face is its own
// texture+sampler pair (the SDL3 GPU backend requires a combined
// texture+sampler at a shared binding index per slot; there is no
// texture-array/cube support in this engine's RHI today, see PLAN). The mesh
// FS's numSamplers therefore goes from 2 to 8 (gAlbedo + gShadowMap pair +
// these 6 pairs) — engine/src/Engine.cpp's two ShaderDesc load call sites
// MUST be updated to match, or pipeline creation fails outright.
[vk::binding(2, 2)]
Texture2D<float> gPointShadowFace0 : register(t2, space2);
[vk::binding(2, 2)]
SamplerState gPointShadowSampler0 : register(s2, space2);

[vk::binding(3, 2)]
Texture2D<float> gPointShadowFace1 : register(t3, space2);
[vk::binding(3, 2)]
SamplerState gPointShadowSampler1 : register(s3, space2);

[vk::binding(4, 2)]
Texture2D<float> gPointShadowFace2 : register(t4, space2);
[vk::binding(4, 2)]
SamplerState gPointShadowSampler2 : register(s4, space2);

[vk::binding(5, 2)]
Texture2D<float> gPointShadowFace3 : register(t5, space2);
[vk::binding(5, 2)]
SamplerState gPointShadowSampler3 : register(s5, space2);

[vk::binding(6, 2)]
Texture2D<float> gPointShadowFace4 : register(t6, space2);
[vk::binding(6, 2)]
SamplerState gPointShadowSampler4 : register(s6, space2);

[vk::binding(7, 2)]
Texture2D<float> gPointShadowFace5 : register(t7, space2);
[vk::binding(7, 2)]
SamplerState gPointShadowSampler5 : register(s7, space2);

// Fragment uniform slot 3 (SPIRV set=3 binding=3, MSL buffer(3)). faceMatrices
// are the SAME 6 view-projections used to render each face in
// point_shadow_depth.slang (ds::pointShadowFaceMatrix on the CPU); reusing
// them here (rather than hand-deriving cubemap UV formulas) guarantees the
// sampling UV is the exact inverse of however the face was rendered.
// lightPosActive.xyz = the shadow-casting light's world position,
// lightPosActive.w = 1.0 if a light is shadow-casting this frame, else 0.0.
struct PointShadowData {
    column_major float4x4 faceMatrices[6];
    float4 lightPosActive;
};

[vk::binding(3, 3)]
ConstantBuffer<PointShadowData> gPointShadow : register(b3, space3);

// Dominant-axis cube-face selection for the direction FROM the light TO a
// point (mirrors ds::cubeFaceDirection's index order: 0=+X,1=-X,2=+Y,3=-Y,
// 4=+Z,5=-Z).
int selectShadowFace(float3 toPoint) {
    float ax = abs(toPoint.x), ay = abs(toPoint.y), az = abs(toPoint.z);
    if (ax >= ay && ax >= az)
        return toPoint.x > 0.0 ? 0 : 1;
    if (ay >= ax && ay >= az)
        return toPoint.y > 0.0 ? 2 : 3;
    return toPoint.z > 0.0 ? 4 : 5;
}

// Samples the stored distance for `face` at the given world position via that
// face's own light-space matrix (same projection used to render it).
// Out-of-frustum (shouldn't normally happen since the face was chosen by
// dominant axis, but a fragment near a face boundary can straddle) returns a
// large distance so the caller treats it as unshadowed.
float sampleShadowFaceDistance(int face, float3 posWS) {
    float4x4 m  = gPointShadow.faceMatrices[face];
    float4 clip = mul(m, float4(posWS, 1.0));
    if (clip.w <= 0.0)
        return 1e9;
    float3 ndc = clip.xyz / clip.w;
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0)
        return 1e9;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 1.0 - (ndc.y * 0.5 + 0.5));

    if (face == 0)
        return gPointShadowFace0.Sample(gPointShadowSampler0, uv).r;
    if (face == 1)
        return gPointShadowFace1.Sample(gPointShadowSampler1, uv).r;
    if (face == 2)
        return gPointShadowFace2.Sample(gPointShadowSampler2, uv).r;
    if (face == 3)
        return gPointShadowFace3.Sample(gPointShadowSampler3, uv).r;
    if (face == 4)
        return gPointShadowFace4.Sample(gPointShadowSampler4, uv).r;
    return gPointShadowFace5.Sample(gPointShadowSampler5, uv).r;
}

// Fraction of `lightPos`'s light that reaches `posWS`: 1 = fully lit, 0 =
// fully shadowed. Single-tap (no PCF) for a first cut — soften later once
// the base technique is confirmed working on real hardware.
float samplePointShadow(float3 posWS, float3 lightPos) {
    float3 toPoint = posWS - lightPos;
    float dist     = length(toPoint);
    int face       = selectShadowFace(toPoint);
    float stored   = sampleShadowFaceDistance(face, posWS);

    float bias = max(0.05, dist * 0.01);
    return (dist - bias) > stored ? 0.0 : 1.0;
}

// Fraction of the directional sun that reaches `posWS`. 1 = fully lit, 0 =
// fully shadowed. Projects into light clip space, does the [0,1] depth-range
// compare (GLM_FORCE_DEPTH_ZERO_TO_ONE), applies a slope-scaled bias and a 3x3
// PCF kernel. Fragments outside the light frustum are treated as lit.
float sampleSunShadow(float3 posWS, float NdotL) {
    float4 clip = mul(gShadow.lightSpace, float4(posWS, 1.0));
    // Orthographic light matrix => w == 1, but divide for safety.
    float3 ndc = clip.xyz / clip.w;

    // Outside the light frustum (x/y in NDC [-1,1], depth [0,1]) => no shadow.
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0;

    // NDC xy [-1,1] -> uv [0,1]; flip Y to match the sampled-texture origin.
    float2 uv = float2(ndc.x * 0.5 + 0.5, 1.0 - (ndc.y * 0.5 + 0.5));

    // Slope-scaled bias: larger at grazing angles to fight self-shadow acne.
    float bias = max(0.0015 * (1.0 - NdotL), 0.0005);
    float currentDepth = ndc.z - bias;

    // 3x3 PCF.
    float res    = max(float(gShadow.params.x), 1.0);
    float2 texel = float2(1.0 / res, 1.0 / res);
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closest = gShadowMap.Sample(gShadowSampler, uv + float2(x, y) * texel).r;
            shadow += currentDepth <= closest ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

// pi, matching ds::kPi used by the CPU reference (engine/include/engine/PbrBrdf.h).
static const float kPi = 3.14159265358979323846;

// ---- Cook-Torrance terms, mirroring ds::PbrBrdf EXACTLY. -------------------

// Trowbridge-Reitz (GGX) NDF, a = roughness^2.
float distributionGGX(float NdotH, float roughness) {
    float a     = roughness * roughness;
    float a2    = a * a;
    float nh    = max(NdotH, 0.0);
    float denom = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (kPi * denom * denom);
}

// Smith geometry for direct lighting, k = (roughness + 1)^2 / 8.
float geometrySmith(float NdotV, float NdotL, float roughness) {
    float r    = roughness + 1.0;
    float k     = (r * r) / 8.0;
    float nv    = max(NdotV, 0.0);
    float nl    = max(NdotL, 0.0);
    float ggxV  = nv / (nv * (1.0 - k) + k);
    float ggxL  = nl / (nl * (1.0 - k) + k);
    return ggxV * ggxL;
}

// Fresnel-Schlick: F0 + (1 - F0) * (1 - cosTheta)^5.
float3 fresnelSchlick(float cosTheta, float3 F0) {
    float c  = 1.0 - max(cosTheta, 0.0);
    float c5 = c * c * c * c * c;
    return F0 + (float3(1.0, 1.0, 1.0) - F0) * c5;
}

// F0 = mix(0.04, albedo, metallic).
float3 computeF0(float3 albedo, float metallic) {
    return lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
}

// Full Cook-Torrance shading for one light. Returns vec3(0) when NdotL <= 0.
float3 cookTorrance(float3 N, float3 V, float3 L, float3 albedo, float metallic,
                    float roughness, float3 radiance) {
    float NdotL = dot(N, L);
    if (NdotL <= 0.0)
        return float3(0.0, 0.0, 0.0);

    float3 H    = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float3 F0 = computeF0(albedo, metallic);
    float D   = distributionGGX(NdotH, roughness);
    float G   = geometrySmith(NdotV, NdotL, roughness);
    float3 F  = fresnelSchlick(HdotV, F0);

    float3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 1e-4;
    float3 specular  = numerator / denominator;

    float3 kd      = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);
    float3 diffuse = kd * albedo / kPi;

    return (diffuse + specular) * radiance * NdotL;
}

// The single directional "sun". Kept as named constants so task 31 (shadows)
// can reuse the SAME light direction.
static const float3 kSunDir    = normalize(float3(0.5, 1.0, 0.3));
static const float3 kSunColor  = float3(1.0, 0.95, 0.85);
static const float  kSunRadiance = 3.0;

[shader("vertex")]
VSOutput vertMain(VSInput v) {
    VSOutput o;

    // Reassemble the per-instance model matrix from the four streamed columns.
    // float4x4(c0,c1,c2,c3) in Slang/HLSL builds the matrix ROW-major from the
    // arguments, so to use m0..m3 as COLUMNS we set the matrix's columns
    // explicitly via transpose of the row-constructed matrix.
    float4x4 model = transpose(float4x4(v.m0, v.m1, v.m2, v.m3));

    float4 worldPos = mul(model, float4(v.pos, 1.0));
    o.position      = mul(gPush.viewProj, worldPos);
    o.color         = v.color;
    o.uv            = v.uv;
    // Normal + position to world space (uniform scale only - no inverse transpose)
    o.normalWS = normalize(mul((float3x3)model, v.normal));
    o.posWS    = worldPos.xyz;
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput i) : SV_Target {
    float4 texColor = gAlbedo.Sample(i.uv);

    // Base color (linear): texture * vertex color * material tint.
    float3 albedo = texColor.rgb * i.color * gMaterial.baseColorTint;

    float metallic  = gMaterial.metallic;
    float roughness = gMaterial.roughness;

    float3 N = normalize(i.normalWS);
    float3 V = normalize(gMaterial.cameraPos - i.posWS);

    // Direct lighting accumulator. Output is LINEAR HDR; the tonemap pass owns
    // the curve, so do NOT gamma/tonemap here.
    float3 Lo = float3(0.0, 0.0, 0.0);

    // Directional sun (shared kSunDir with the shadow pass). The single-cascade
    // shadow map attenuates ONLY this term; point lights stay unshadowed.
    float sunNdotL = max(dot(N, kSunDir), 0.0);
    float sunShadow = sampleSunShadow(i.posWS, sunNdotL);
    Lo += sunShadow * cookTorrance(N, V, kSunDir, albedo, metallic, roughness, kSunColor * kSunRadiance);

    // Point lights with inverse-square falloff smoothly windowed to the radius.
    // Exactly one light (whichever the CPU picked as nearest-to-camera, see
    // gPointShadow.lightPosActive) gets real shadow occlusion; every other
    // light stays unshadowed, exactly as before this feature.
    bool pointShadowActive = gPointShadow.lightPosActive.w > 0.5;
    int count = min(gLights.count.x, kMaxLights);
    for (int li = 0; li < count; ++li) {
        Light L        = gLights.lights[li];
        float3 toLight = L.posRadius.xyz - i.posWS;
        float  dist    = length(toLight);
        float  radius  = L.posRadius.w;
        if (dist > radius)
            continue;
        float3 ldir   = toLight / max(dist, 1e-4);
        float  atten  = 1.0 / (1.0 + dist * dist);
        float  window = saturate(1.0 - (dist / radius));
        atten        *= window * window;

        if (pointShadowActive && distance(L.posRadius.xyz, gPointShadow.lightPosActive.xyz) < 0.01)
            atten *= samplePointShadow(i.posWS, L.posRadius.xyz);

        float3 radiance = L.colorIntensity.rgb * L.colorIntensity.w * atten;
        Lo += cookTorrance(N, V, ldir, albedo, metallic, roughness, radiance);
    }

    // Small ambient so unlit areas are never pure black.
    float3 ambient = float3(0.12, 0.12, 0.15) * albedo;
    float3 color   = ambient + Lo;

    return float4(color, texColor.a);
}
