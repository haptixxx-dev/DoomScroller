// Point-light shadow depth pass (distance-based, one of 6 cube faces).
//
// Unlike the sun's single-cascade shadow map (shadow_depth.slang, which is
// purely depth-only), a point light's 6 perspective frustums don't compare
// cleanly via raw NDC depth across faces. Instead this pass writes LINEAR
// DISTANCE from the light into a single-channel color target (R32Float); the
// lighting pass (mesh.slang) later compares a fragment's actual distance to
// the light against this stored value. A companion depth attachment is still
// bound for the pass's own hidden-surface test, but it is never sampled
// afterward (see engine/src/Engine.cpp's m_pointShadowDepthScratch — one
// scratch depth buffer shared and re-cleared across all 6 face passes).
//
// Reuses the SAME vertex input layout as mesh.slang/shadow_depth.slang so the
// existing mesh vertex/index buffers draw unchanged.

struct VSInput {
    float3 pos    : POSITION0;
    float3 color  : COLOR0;
    float2 uv     : TEXCOORD0;
    float3 normal : NORMAL0;
    // Per-instance model matrix columns (matches mesh.slang's instanced
    // vertex stream, slot 1, locations 4..7).
    float4 m0 : TEXCOORD2;
    float4 m1 : TEXCOORD3;
    float4 m2 : TEXCOORD4;
    float4 m3 : TEXCOORD5;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
};

// Vertex uniform: this face's light-space view-projection, plus the light's
// world position (needed in the fragment stage to compute distance). The
// per-object model matrix is the instanced vertex stream, so the shadow
// position is lightSpace * model * pos assembled in-shader.
struct PushConstants {
    column_major float4x4 lightSpace;
    float3 lightPos;
    float pad0;
};

[vk::binding(0, 1)]
ConstantBuffer<PushConstants> gPush : register(b0);

[shader("vertex")]
VSOutput vertMain(VSInput v) {
    VSOutput o;
    float4x4 model  = transpose(float4x4(v.m0, v.m1, v.m2, v.m3));
    float4 worldPos = mul(model, float4(v.pos, 1.0));
    o.position      = mul(gPush.lightSpace, worldPos);
    o.worldPos      = worldPos.xyz;
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput i) : SV_Target {
    float dist = length(i.worldPos - gPush.lightPos);
    return float4(dist, 0.0, 0.0, 1.0);
}
