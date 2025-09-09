#include "ReShade.fxh"

texture2D GlassMaskTex
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = RGBA8;
};
sampler2D GlassMaskSampler { Texture = GlassMaskTex; };

float4 PS_GlassMask(float4 vpos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    return tex2D(GlassMaskSampler, texcoord);
}

technique GlassMaskView
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = PS_GlassMask;
    }
}