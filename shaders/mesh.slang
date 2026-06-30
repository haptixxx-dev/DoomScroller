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
    float3 posWS    : TEXCOORD1;
};

struct PushConstants {
    column_major float4x4 mvp;
    column_major float4x4 model;
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

// Fragment texture+sampler slot 0: SPIRV set=2 binding=0, MSL texture(0)/sampler(0)
[vk::binding(0, 2)]
Texture2D<float4> gAlbedo : register(t0, space2);

[vk::binding(0, 2)]
SamplerState gSampler : register(s0, space2);

// Fragment uniform slot 0: SDL3 GPU maps fragment uniform buffers to SPIRV
// set=3 (MSL buffer(0)). Filled via pushFragmentConstants each frame.
[vk::binding(0, 3)]
ConstantBuffer<LightData> gLights : register(b0, space3);

[shader("vertex")]
VSOutput vertMain(VSInput v) {
    VSOutput o;
    o.position = mul(gPush.mvp, float4(v.pos, 1.0));
    o.color    = v.color;
    o.uv       = v.uv;
    // Normal + position to world space (uniform scale only - no inverse transpose)
    o.normalWS = normalize(mul((float3x3)gPush.model, v.normal));
    o.posWS    = mul(gPush.model, float4(v.pos, 1.0)).xyz;
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput i) : SV_Target {
    float4 texColor = gAlbedo.Sample(gSampler, i.uv);

    float3 n = normalize(i.normalWS);

    // Keep a directional ambient "sun" so unlit areas are never pure black.
    float3 sunDir   = normalize(float3(0.5, 1.0, 0.3));
    float3 sunColor = float3(1.0, 0.95, 0.85);
    float3 ambient  = float3(0.12, 0.12, 0.15);
    float  sunDiff  = max(0.0, dot(n, sunDir));
    float3 lit      = ambient + sunDiff * sunColor * 0.5;

    // Accumulate point lights with inverse-square falloff clamped to radius.
    int count = min(gLights.count.x, kMaxLights);
    for (int li = 0; li < count; ++li) {
        Light L        = gLights.lights[li];
        float3 toLight = L.posRadius.xyz - i.posWS;
        float  dist    = length(toLight);
        float  radius  = L.posRadius.w;
        if (dist > radius)
            continue;
        float3 ldir   = toLight / max(dist, 1e-4);
        float  diff   = max(0.0, dot(n, ldir));
        // 1/d^2 falloff, smoothly windowed to zero at the radius.
        float  atten  = 1.0 / (1.0 + dist * dist);
        float  window = saturate(1.0 - (dist / radius));
        atten        *= window * window;
        lit          += diff * L.colorIntensity.rgb * L.colorIntensity.w * atten;
    }

    return texColor * float4(i.color, 1.0) * float4(lit, 1.0);
}
