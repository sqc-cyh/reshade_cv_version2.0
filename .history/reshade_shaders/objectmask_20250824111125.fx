#include "ReShade.fxh"

uniform bool ObjectMask < // <--- 将 "GlassMask" 修改为 "ObjectMask"
    ui_label = "Enable Object Mask"; // <--- 建议也将 UI 标签修改一下
> = false;

float4 PS_ObjectMask(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target // <--- 建议也将函数名修改
{
    return float4(0,0,0,0);
}

technique ObjectMask
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader  = PS_ObjectMask; // <--- 对应修改

        BlendEnable    = true;
        SrcBlend       = ZERO;
        DestBlend      = ONE;
        BlendOp        = ADD;
        SrcBlendAlpha  = ZERO;
        DestBlendAlpha = ONE;
        BlendOpAlpha   = ADD;
    }
}