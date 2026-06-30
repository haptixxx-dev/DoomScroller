// Depth-only shadow pass (task 31).
//
// Renders the scene from the sun's point of view into a single-cascade
// directional shadow map (D32Float). The vertex stage transforms the position
// by a light-space MVP (lightSpace * model) pushed as a vertex push constant;
// only depth is written. SDL3 GPU still needs a fragment stage for a graphics
// pipeline, so a trivial fragment is provided — it has no color target bound
// (the pipeline declares zero color targets), so its output is discarded and
// only the depth buffer is written.
//
// Reuses the SAME vertex input layout as mesh.slang so the existing mesh
// vertex/index buffers can be drawn unchanged. The push-constant layout MUST
// match the mesh vertex stage's binding (vk::binding(0,1) / register(b0) /
// MSL buffer(0)). We only need the combined light-space MVP here.

struct VSInput {
    float3 pos    : POSITION0;
    float3 color  : COLOR0;
    float2 uv     : TEXCOORD0;
    float3 normal : NORMAL0;
    // Per-instance model matrix columns (task 28), matching mesh.slang's
    // instanced vertex stream (slot 1, locations 4..7). m0..m3 are the columns.
    float4 m0 : TEXCOORD2;
    float4 m1 : TEXCOORD3;
    float4 m2 : TEXCOORD4;
    float4 m3 : TEXCOORD5;
};

struct VSOutput {
    float4 position : SV_Position;
};

// Vertex uniform: the light-space view-projection. The per-object model matrix
// is the instanced vertex stream now, so the shadow position is
// lightSpace * model * pos assembled in-shader (task 28).
struct PushConstants {
    column_major float4x4 lightSpace;
};

[vk::binding(0, 1)]
ConstantBuffer<PushConstants> gPush : register(b0);

[shader("vertex")]
VSOutput vertMain(VSInput v) {
    VSOutput o;
    float4x4 model = transpose(float4x4(v.m0, v.m1, v.m2, v.m3));
    float4 worldPos = mul(model, float4(v.pos, 1.0));
    o.position      = mul(gPush.lightSpace, worldPos);
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput i) : SV_Target {
    // No color target is bound for this pipeline; only depth is written.
    return float4(0.0, 0.0, 0.0, 1.0);
}
