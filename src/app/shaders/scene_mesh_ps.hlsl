cbuffer Scene : register(b0) {
    row_major float4x4 viewProj;
    float4 offset;
    float4 color;
    float4 lightDir;
    float4 params;
};

struct PsIn {
    float4 pos : SV_POSITION;
    float3 nrm : NORMAL;
};

float4 main(PsIn p) : SV_TARGET {
    if (params.x > 0.5) {
        return color;
    }
    // Two-sided Lambert from a headlight: STL winding is not reliable, so both faces are lit.
    float3 n = normalize(p.nrm);
    float diffuse = abs(dot(n, -lightDir.xyz));
    return float4(color.rgb * (0.35 + 0.65 * diffuse), color.a);
}
