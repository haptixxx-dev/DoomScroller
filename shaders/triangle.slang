struct VSInput {
    float3 pos   : POSITION0;
    float3 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

struct MVPConstants {
    column_major float4x4 mvp;
};

// SPIRV: descriptor set 1, binding 0 (SDL3 vertex uniform slot 0)
// MSL:   [[buffer(1)]]  (after vertex buffer at slot 0)
[vk::binding(0, 1)]
ConstantBuffer<MVPConstants> gMVP : register(b1);

[shader("vertex")]
VSOutput vertMain(VSInput v) {
    VSOutput out;
    out.position = mul(gMVP.mvp, float4(v.pos, 1.0));
    out.color    = v.color;
    return out;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
