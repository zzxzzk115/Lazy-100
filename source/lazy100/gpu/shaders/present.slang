// present.slang - Lazy-100 full-screen palette resolve.
//
// A no-vertex-buffer full-screen triangle samples the R8_UINT index framebuffer with an
// integer texel fetch (.Load, so no sampler is needed) and looks the index up in the
// 256-entry palette. The palette lives in a constant buffer as uint4[64] (tight 1024 bytes;
// a `uint colors[256]` would get 16-byte HLSL cbuffer array stride and not match the
// 256-contiguous-uint upload from the CPU).

struct Palette
{
    uint4 colors[64]; // 256 RGBA8-packed entries (r | g<<8 | b<<16 | a<<24), 4 per row
};

[[vk::binding(0, 0)]] ConstantBuffer<Palette> uPalette;
// The index framebuffer is an R8_UNORM texture, not R8_UINT: integer textures upload through
// GL_RED_INTEGER, a path the GL backend's PBO upload doesn't take (GL_INVALID_OPERATION),
// whereas a normalized R8 uploads normally. 8-bit unorm has exactly 256 levels, so the palette
// index round-trips exactly (recover it with round(v * 255)). Pin the format to r8 so Slang
// doesn't default the sampled image to something else.
[[vk::image_format("r8")]] [[vk::binding(1, 0)]] Texture2D<float> uIndex;

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOut vertexMain(uint vid: SV_VertexID)
{
    float2 uv = float2((vid << 1) & 2, vid & 2); // (0,0) (2,0) (0,2)
    VSOut  o;
    o.uv = uv;
    // VRI clip space is Y-up, so flip Y: uv.y=0 (framebuffer top row) -> clip top (+1),
    // otherwise the image presents upside down.
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOut i): SV_Target
{
    int2 dim = int2(320, 240);
    int2 p   = clamp(int2(i.uv * float2(dim)), int2(0, 0), dim - 1);
    uint idx = uint(uIndex.Load(int3(p, 0)) * 255.0 + 0.5); // R8_UNORM [0,1] -> palette index [0,255]
    uint packed = uPalette.colors[idx >> 2][idx & 3];
    return float4(float(packed & 0xffu), float((packed >> 8) & 0xffu), float((packed >> 16) & 0xffu), 255.0) /
           255.0;
}
