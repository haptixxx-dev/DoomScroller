// Bootstrap triangle shader - no vertex buffer, positions generated in shader.
// Draws a single full-screen-ish triangle using vertex index.

struct VSOutput {
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID) {
    // Clip-space positions for a CCW triangle covering most of the screen
    float2 positions[3] = {
        float2( 0.0,  0.5),
        float2( 0.5, -0.5),
        float2(-0.5, -0.5),
    };

    float3 colors[3] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, 1.0),
    };

    VSOutput out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    out.color    = colors[vertexID];
    return out;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
