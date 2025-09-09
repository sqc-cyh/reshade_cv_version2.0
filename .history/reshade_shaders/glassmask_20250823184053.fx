#include "ReShade.fxh"

texture2D SceneColorTex < source = "color"; >
{
    Width  = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = RGBA8;
};
sampler2D SceneColor { Texture = SceneColorTex; };

float4 PS_GlassMask(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return tex2D(SceneColor, uv);     // 原样拷贝
}

technique GlassMask
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader  = PS_GlassMask;
    }
}
