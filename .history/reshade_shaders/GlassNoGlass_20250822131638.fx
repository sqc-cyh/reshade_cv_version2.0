// GlassNoGlass.fx

texture Glass_NoGlassTex < string UIName = "Glass_NoGlassTex"; > { Width = 0; Height = 0; Format = RGBA8; };
sampler GlassNoGlassSamp { Texture = Glass_NoGlassTex; };

texture backbufferTex : COLOR;
sampler BackSamp { Texture = backbufferTex; };

uniform bool ShowNoGlass < ui_type = "checkbox"; ui_label = "Show No-Glass Texture"; > = true;

float4 PS_Main(float4 pos : SV_Position, float2 uv : TexCoord) : SV_Target
{
    float4 src = BackSamp.Load(int3(uv * backbufferTex.GetDimensions(), 0));
    float4 nog = GlassNoGlassSamp.Load(int3(uv * Glass_NoGlassTex.GetDimensions(), 0));

    // 直接显示“无玻璃”
    if (ShowNoGlass) return nog;

    // 或做对比：左侧显示无玻璃，右侧显示原图
    if (uv.x < 0.5) return nog;
    else return src;
}

technique ShowGlassless
{
    pass P0 { VertexShader = PostProcessVS; PixelShader = PS_Main; }
}
