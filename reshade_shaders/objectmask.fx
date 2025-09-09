#include "ReShade.fxh"

float4 PS_ObjectMask(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return float4(0,0,0,0);          
}

technique ObjectMask
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader  = PS_ObjectMask;

        BlendEnable    = true;       
        SrcBlend       = ZERO;
        DestBlend      = ONE;
        BlendOp        = ADD;
        SrcBlendAlpha  = ZERO;
        DestBlendAlpha = ONE;
        BlendOpAlpha   = ADD;
    }
}
