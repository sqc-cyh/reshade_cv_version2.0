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
    float4 v = tex2D(GlassMaskSampler, texcoord);
    return float4(v.rgb, 0.0); 
}

technique GlassMask
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader  = PS_GlassMask;
    }
}
