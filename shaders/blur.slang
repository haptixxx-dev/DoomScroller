// Separable Gaussian blur for the bloom chain (task 26).
//
// Fullscreen SV_VertexID-triangle pass. Used twice per frame: once horizontal,
// once vertical, by pushing a `direction` of (1/width, 0) then (0, 1/height).
// The 5-tap (radius 2) weights are BAKED to match ds::gaussianKernel(2, 1.0)
// in engine/include/engine/BloomKernel.h EXACTLY (normalized, sum == 1):
//   center   = 0.40261994689424746
//   offset 1 = 0.24420134200323335   (each side)
//   offset 2 = 0.054488684549642945  (each side)
// Sums to 1.0 within float precision. If BloomKernel.h's radius/sigma change,
// regenerate these constants to keep the GPU result matching the CPU reference.

struct VSOutput {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

// Fragment uniform slot 0 (SPIRV set=3, MSL buffer(0)): blur step direction in
// UV space. Horizontal pass pushes (texelW, 0); vertical pushes (0, texelH).
struct BlurConstants {
    float2 direction;
    float2 pad;
};

[vk::binding(0, 3)]
ConstantBuffer<BlurConstants> gBlur : register(b0, space3);

// Fragment texture+sampler slot 0 (SPIRV set=2 binding=0): the source to blur.
[vk::binding(0, 2)]
Texture2D<float4> gSrc : register(t0, space2);

[vk::binding(0, 2)]
SamplerState gSampler : register(s0, space2);

[shader("vertex")]
VSOutput vertMain(uint id : SV_VertexID) {
    VSOutput o;
    float2 pos = float2((id == 1) ? 3.0 : -1.0, (id == 2) ? 3.0 : -1.0);
    o.position = float4(pos, 0.0, 1.0);
    o.uv       = float2(pos.x * 0.5 + 0.5, 1.0 - (pos.y * 0.5 + 0.5));
    return o;
}

// Baked normalized weights from ds::gaussianKernel(2, 1.0). Index 0 = center.
static const float kW0 = 0.40261994689424746;
static const float kW1 = 0.24420134200323335;
static const float kW2 = 0.054488684549642945;

[shader("fragment")]
float4 fragMain(VSOutput i) : SV_Target {
    float2 dir = gBlur.direction;

    float3 sum = gSrc.Sample(gSampler, i.uv).rgb * kW0;
    sum += gSrc.Sample(gSampler, i.uv + dir * 1.0).rgb * kW1;
    sum += gSrc.Sample(gSampler, i.uv - dir * 1.0).rgb * kW1;
    sum += gSrc.Sample(gSampler, i.uv + dir * 2.0).rgb * kW2;
    sum += gSrc.Sample(gSampler, i.uv - dir * 2.0).rgb * kW2;

    return float4(sum, 1.0);
}
