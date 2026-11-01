// Geometry viewport: nodal temperatures as camera-facing round points of a fixed pixel size.
// One instance per node; the four corners of its quad come from SV_VertexID (triangle strip).
cbuffer Points : register(b0) {
    row_major float4x4 viewProj;
    float4 offset;   // xyz: point-set origin − scene origin, m
    float4 viewport; // x, y: viewport size in px; z: point diameter in px
};

struct VsIn {
    float3 pos : POSITION; // per instance
    float value : TEXCOORD0; // normalised temperature in [0, 1], per instance
    uint vid : SV_VertexID;
};

struct VsOut {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float value : TEXCOORD1;
};

VsOut main(VsIn v) {
    float2 corner = float2((v.vid & 2) ? 1.0 : -1.0, (v.vid & 1) ? 1.0 : -1.0);
    VsOut o;
    o.pos = mul(viewProj, float4(v.pos + offset.xyz, 1.0));
    // NDC spans 2 units per viewport, so a radius of size/2 px is size/viewport in NDC.
    o.pos.xy += corner * (viewport.z / viewport.xy) * o.pos.w;
    o.uv = corner;
    o.value = v.value;
    return o;
}
