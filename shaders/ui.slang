// 2D screen-space UI overlay: textured, colored quads drawn after the 3D
// scene with alpha blending and no depth test.
//
// Vertices carry screen-space pixel positions, a uv, and an rgba color. The
// vertex shader maps pixel coords to clip space using a push constant holding
// (2/screenW, 2/screenH): clip = pos * scale - 1, with Y flipped so (0,0) is
// the top-left corner. The fragment shader returns sampledTexture * color, so
// solid quads can bind a 1x1 white texture and glyph quads bind the font atlas.

struct VSInput {
    float2 pos   : POSITION0; // screen pixels, origin top-left
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct PushConstants {
    float2 invScreen; // (2/screenW, 2/screenH)
    float2 pad;
};

// Vertex uniform slot 0 (MSL buffer(0), SPIRV set=1 binding=0).
[vk::binding(0, 1)]
ConstantBuffer<PushConstants> gPush : register(b0);

// Fragment texture+sampler slot 0: SPIRV set=2 binding=0 as combined image sampler.
[vk::binding(0, 2)]
Sampler2D gTex;

[shader("vertex")]
VSOutput vertMain(VSInput v) {
    VSOutput o;
    // pixel -> NDC: x in [-1,1], y flipped so top-left is origin.
    float2 ndc = float2(v.pos.x * gPush.invScreen.x - 1.0, 1.0 - v.pos.y * gPush.invScreen.y);
    o.position = float4(ndc, 0.0, 1.0);
    o.uv       = v.uv;
    o.color    = v.color;
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput i) : SV_Target {
    float4 tex = gTex.Sample(i.uv);
    return tex * i.color;
}
