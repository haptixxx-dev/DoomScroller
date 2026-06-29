struct VSInput {
    float3 pos    : POSITION0;
    float3 color  : COLOR0;
    float2 uv     : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 color    : COLOR0;
    float2 uv       : TEXCOORD0;
    float3 normalWS : NORMAL0;
};

struct PushConstants {
    column_major float4x4 mvp;
    column_major float4x4 model;
};

// Vertex uniform slot 0. SDL3 GPU Metal binds vertex uniform buffers at
// buffer(0..N) and vertex INPUT buffers at buffer(14+), so push constants
// MUST be MSL buffer(0). SPIRV set=1 binding=0 (via vk::binding), DXIL b0.
[vk::binding(0, 1)]
ConstantBuffer<PushConstants> gPush : register(b0);

// Fragment texture+sampler slot 0: SPIRV set=2 binding=0, MSL texture(0)/sampler(0)
[vk::binding(0, 2)]
Texture2D<float4> gAlbedo : register(t0, space2);

[vk::binding(0, 2)]
SamplerState gSampler : register(s0, space2);

[shader("vertex")]
VSOutput vertMain(VSInput v) {
    VSOutput o;
    o.position = mul(gPush.mvp, float4(v.pos, 1.0));
    o.color    = v.color;
    o.uv       = v.uv;
    // Normal to world space (uniform scale only - no inverse transpose needed)
    o.normalWS = normalize(mul((float3x3)gPush.model, v.normal));
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput i) : SV_Target {
    float4 texColor = gAlbedo.Sample(gSampler, i.uv);

    // Hardcoded directional light (sun from upper-right-front)
    float3 lightDir   = normalize(float3(0.5, 1.0, 0.3));
    float3 lightColor = float3(1.0, 0.95, 0.85);
    float3 ambient    = float3(0.12, 0.12, 0.15);

    float3 n    = normalize(i.normalWS);
    float  diff = max(0.0, dot(n, lightDir));
    float3 lit  = ambient + diff * lightColor;

    return texColor * float4(i.color, 1.0) * float4(lit, 1.0);
}
