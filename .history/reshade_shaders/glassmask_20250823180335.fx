#include "ReShade.fxh"

float4 PS_GlassMask(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(0.0, 0.0, 0.0, 0.0); // 全透明
}

technique GlassMask
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader  = PS_GlassMask;

        // 结果 = 旧帧（不改画面）
        BlendEnable    = true;
        SrcBlend       = ZERO;
        DestBlend      = ONE;
        BlendOp        = ADD;
        SrcBlendAlpha  = ZERO;
        DestBlendAlpha = ONE;
        BlendOpAlpha   = ADD;
    }
}
