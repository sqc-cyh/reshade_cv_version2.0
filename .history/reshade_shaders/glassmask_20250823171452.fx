#include "ReShade.fxh"

// 可选：如果你不需要显示任何内容，这个资源块可以保留也可以删除。
// 留下它的好处是勾选 GlassMask 时你也能把一张调试图画出来。
texture2D GlassMaskTex
{
    Width = BUFFER_WIDTH;
    Height = BUFFER_HEIGHT;
    Format = RGBA8;
};
sampler2D GlassMaskSampler { Texture = GlassMaskTex; };

// 如果没有写入 GlassMaskTex，就返回全透明，不影响最终画面。
float4 PS_GlassMask(float4 vpos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    float4 v = tex2D(GlassMaskSampler, texcoord);
    return float4(v.rgb, 0.0); // 全透明；仅作为开关存在
}

// ★ 关键：technique 名称改为 GlassMask，与 C++ 检测一致
technique GlassMask
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader  = PS_GlassMask;
    }
}
