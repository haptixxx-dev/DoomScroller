// Bloom bright-pass prefilter (task 26).
//
// Fullscreen SV_VertexID-triangle pass (same style as tonemap.slang): reads the
// RGBA16Float HDR scene target and writes the over-threshold light into a bloom
// RT (also RGBA16Float). The fragment stage MIRRORS ds::brightPass in
// engine/include/engine/BloomKernel.h EXACTLY: a Rec.709 luminance, then a
// quadratic soft-knee that keeps a pixel's color scaled by how far its luma sits
// above `threshold`, smoothly ramped over a band of half-width `knee`.
//
// RESOLUTION CHOICE: this first cut runs the whole bloom chain at FULL window
// resolution (the bloom RTs are window-sized, same as the HDR target). Half-res
// would be cheaper and give a wider/softer glow per blur tap, but full-res keeps
// the engine wiring simple (one size for HDR + both bloom buffers) and is an
// acceptable first implementation per the task. Revisit with a half-res
// downsample + mip chain if the glow needs to be wider/cheaper.

struct VSOutput {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

// Fragment uniform slot 0 (SPIRV set=3, MSL buffer(0)): bright-pass tuning.
// Pushed via pushFragmentConstants each frame.
struct BrightConstants {
    float threshold;
    float knee;
    float2 pad;
};

[vk::binding(0, 3)]
ConstantBuffer<BrightConstants> gBright : register(b0, space3);

// Fragment texture+sampler slot 0 (SPIRV set=2 binding=0): the HDR scene target.
[vk::binding(0, 2)]
Texture2D<float4> gHDR : register(t0, space2);

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

// Rec.709 luma — mirrors ds::luminance.
float luminance(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Quadratic soft-knee bright-pass — mirrors ds::brightPass.
float3 brightPass(float3 hdr, float threshold, float knee) {
    const float kEps = 1e-6;
    float b          = luminance(hdr);

    float soft = b - threshold + knee;
    soft       = clamp(soft, 0.0, 2.0 * knee);
    soft       = soft * soft / (4.0 * knee + kEps);

    float contribution = max(soft, b - threshold);
    float weight       = max(contribution, 0.0) / max(b, kEps);
    return hdr * weight;
}

[shader("fragment")]
float4 fragMain(VSOutput i) : SV_Target {
    float3 hdr  = gHDR.Sample(gSampler, i.uv).rgb;
    float3 lit  = brightPass(hdr, gBright.threshold, gBright.knee);
    return float4(lit, 1.0);
}
