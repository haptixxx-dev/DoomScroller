// Fullscreen HDR -> LDR tonemap pass (task 21).
//
// The 3D scene + particles are rendered into an RGBA16Float HDR target; this
// pass reads that target and resolves it to the LDR swapchain. The vertex
// stage generates a single full-screen triangle from SV_VertexID (no vertex
// buffer): the three clip positions {(-1,-1),(3,-1),(-1,3)} cover the screen,
// and uv is derived from the clip position. The fragment stage applies an
// exposure multiplier then the ACES filmic curve, MIRRORING engine/Tonemap.h
// (acesFilmic: a=2.51 b=0.03 c=2.43 d=0.59 f=0.14, clamped to [0,1]).

struct VSOutput {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

struct ExposureConstants {
    float exposure;
    float bloomIntensity; // scales the additive bloom contribution (task 26)
    float2 pad;
};

// Fragment uniform slot 0: SDL3 GPU maps fragment uniform buffers to SPIRV
// set=3 (MSL buffer(0)). Filled via pushFragmentConstants each frame.
[vk::binding(0, 3)]
ConstantBuffer<ExposureConstants> gExposure : register(b0, space3);

// Fragment texture+sampler slot 0: SPIRV set=2 binding=0, MSL texture(0)/sampler(0).
[vk::binding(0, 2)]
Texture2D<float4> gHDR : register(t0, space2);

[vk::binding(0, 2)]
SamplerState gSampler : register(s0, space2);

// Fragment texture+sampler slot 1: the blurred bloom result (RGBA16Float), added
// to the HDR sample BEFORE tonemapping (task 26). This FS therefore declares 2
// samplers — keep the ShaderLoader numSamplers in sync (initScene).
[vk::binding(1, 2)]
Texture2D<float4> gBloom : register(t1, space2);

[vk::binding(1, 2)]
SamplerState gBloomSampler : register(s1, space2);

[shader("vertex")]
VSOutput vertMain(uint id : SV_VertexID) {
    VSOutput o;
    // Full-screen triangle: covers the [-1,1] NDC box with a single primitive.
    float2 pos = float2((id == 1) ? 3.0 : -1.0, (id == 2) ? 3.0 : -1.0);
    o.position = float4(pos, 0.0, 1.0);
    // pos*0.5+0.5 maps clip -> [0,1]; flip Y so the sampled image is upright
    // (matches the UI convention where uv origin is top-left).
    o.uv = float2(pos.x * 0.5 + 0.5, 1.0 - (pos.y * 0.5 + 0.5));
    return o;
}

// ACES filmic tonemap — mirrors ds::acesFilmic in engine/Tonemap.h exactly.
float3 acesFilmic(float3 c) {
    const float a = 2.51;
    const float b = 0.03;
    const float d = 2.43;
    const float e = 0.59;
    const float f = 0.14;
    float3 mapped = (c * (a * c + b)) / (c * (c * d + e) + f);
    return clamp(mapped, float3(0.0), float3(1.0));
}

[shader("fragment")]
float4 fragMain(VSOutput i) : SV_Target {
    float3 hdr   = gHDR.Sample(gSampler, i.uv).rgb;
    // Additive bloom composite: add the blurred bright-pass result to the HDR
    // scene BEFORE exposure + ACES so the glow goes through the same curve.
    float3 bloom = gBloom.Sample(gBloomSampler, i.uv).rgb;
    hdr += bloom * gExposure.bloomIntensity;

    float3 exposed = hdr * gExposure.exposure;
    float3 ldr     = acesFilmic(exposed);
    return float4(ldr, 1.0);
}
