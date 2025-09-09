#include "ReShade.fxh"

float4 PS_GlassMask(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(0,0,0,0);           // 输出全 0
}

technique GlassMask
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader  = PS_GlassMask;

        BlendEnable    = true;        // 结果 = 原帧
        SrcBlend       = ZERO;
        DestBlend      = ONE;
        BlendOp        = ADD;
        SrcBlendAlpha  = ZERO;
        DestBlendAlpha = ONE;
        BlendOpAlpha   = ADD;
    }
}
