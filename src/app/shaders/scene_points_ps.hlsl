Texture1D colormap : register(t0); // inferno, 256 texels
SamplerState linearClamp : register(s0);

struct PsIn {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float value : TEXCOORD1;
};

float4 main(PsIn p) : SV_TARGET {
    float r2 = dot(p.uv, p.uv);
    if (r2 > 1.0) {
        discard;
    }
    float3 c = colormap.Sample(linearClamp, p.value).rgb;
    // Thin darker rim so neighbouring points stay distinguishable.
    float rim = r2 > 0.72 ? 0.65 : 1.0;
    return float4(c * rim, 1.0);
}
