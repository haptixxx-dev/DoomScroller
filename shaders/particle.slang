// Camera-facing billboard particles drawn via GPU instancing.
//
// Each instance supplies a world position, a size and an rgba color through an
// instanced vertex buffer (binding 0, see ParticleSystem::Instance). The quad
// corner is generated from SV_VertexID (6 verts per instance, two triangles),
// then billboarded onto the camera's right/up vectors supplied in push
// constants.

struct VSInput {
    // Per-instance (instanced vertex binding 0)
    float3 instancePos : TEXCOORD0;
    float instanceSize : TEXCOORD1;
    float4 instanceColor : TEXCOORD2;
    uint vertexID : SV_VertexID;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color    : COLOR0;
    float2 uv       : TEXCOORD0;
};

struct PushConstants {
    column_major float4x4 viewProj;
    float4 camRight; // xyz used
    float4 camUp;    // xyz used
};

// Vertex uniform slot 0 (MSL buffer(0), SPIRV set=1 binding=0).
[vk::binding(0, 1)]
ConstantBuffer<PushConstants> gPush : register(b0);

[shader("vertex")]
VSOutput vertMain(VSInput v) {
    // Unit quad corners for a 6-vertex (two-triangle) draw.
    const float2 kCorners[6] = {
        float2(-0.5, -0.5), float2(0.5, -0.5), float2(0.5, 0.5),
        float2(-0.5, -0.5), float2(0.5, 0.5),  float2(-0.5, 0.5),
    };

    float2 corner = kCorners[v.vertexID % 6];

    float3 right = gPush.camRight.xyz;
    float3 up    = gPush.camUp.xyz;

    float3 worldPos = v.instancePos + (right * corner.x + up * corner.y) * v.instanceSize;

    VSOutput o;
    o.position = mul(gPush.viewProj, float4(worldPos, 1.0));
    o.color    = v.instanceColor;
    o.uv       = corner + 0.5; // 0..1
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput i) : SV_Target {
    // Soft round falloff so the square quad reads as a glowing dot.
    float2 d  = i.uv - 0.5;
    float r2  = dot(d, d);
    float fade = saturate(1.0 - r2 * 4.0);
    float alpha = i.color.a * fade;
    return float4(i.color.rgb * alpha, alpha);
}
