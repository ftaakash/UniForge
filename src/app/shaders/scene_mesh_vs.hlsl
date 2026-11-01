// Geometry viewport: flat-shaded STL triangles (and the same triangles again as wireframe).
// Positions arrive recentred on the scene origin in float; `offset` moves the mesh's own
// origin to the scene origin (spec §5.3: recentre before the float conversion).
cbuffer Scene : register(b0) {
    row_major float4x4 viewProj;
    float4 offset;   // xyz: model origin − scene origin, m
    float4 color;    // fill or wire colour, linear RGBA
    float4 lightDir; // xyz: direction the light travels (camera forward: a headlight)
    float4 params;   // x: 1 = wireframe pass (flat colour, no shading)
};

struct VsIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
};

struct VsOut {
    float4 pos : SV_POSITION;
    float3 nrm : NORMAL;
};

VsOut main(VsIn v) {
    VsOut o;
    o.pos = mul(viewProj, float4(v.pos + offset.xyz, 1.0));
    o.nrm = v.nrm;
    return o;
}
