// Dear ImGui DX11 pixel shader, extracted unchanged from third_party/imgui/backends/
// imgui_impl_dx11.cpp (MIT, Omar Cornut) so it can be compiled offline with fxc (ps_4_0).
struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};

sampler sampler0;
Texture2D texture0;

float4 main(PS_INPUT input) : SV_Target
{
    float4 out_col = input.col * texture0.Sample(sampler0, input.uv);
    return out_col;
}
