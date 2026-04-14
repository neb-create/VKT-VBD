import Raytracing;
import PBR;
import sh_sampling;
import probe_visibility;
import probe_helper;
import math_helper;

struct UniformBuffer {
    float off;
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};

struct UEntity {
    float4x4 transform;
};

ConstantBuffer<UniformBuffer> ubo;
ConstantBuffer<UEntity> uTransform;
ConstantBuffer<UProbeVolumeLayout> probeLayout;
StructuredBuffer<float> depthBuffer;
[[vk::image_format("r32f")]] Sampler2D<float> depthTexture;

struct VSInput {
    float3 position;
    float3 color;
    float2 uv;
    float3 norm;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 worldPos;
    float3 color;
    float2 uv;
    float3 norm;
};

struct FSInput {
    float4 position : SV_Position;
    float3 worldPos;
    float3 color;
    float2 uv;
    float3 norm;
    uint primID : SV_PrimitiveID;
};

[shader("vertex")]
VSOutput vertMain(VSInput vIn) { //uint vid : SV_VertexID) {
    VSOutput vOut;
    float3 objPos = vIn.position;
    float4 worldPos = mul(uTransform.transform, float4(objPos, 1.0));
    vOut.worldPos = worldPos.xyz;
    vOut.position = mul(ubo.proj, mul(ubo.view, worldPos));
    vOut.color = vIn.color;
    vOut.uv = vIn.uv;
    vOut.norm = mul(uTransform.transform, float4(vIn.norm, 0.0)).xyz; // make do opposite scale (inv trans)
    return vOut;
}

#define DEBUG_OUT_OF_BOUNDS 1

float SampleInterpolatedDepth(float3 rd, float3 pos) {
    float3 lp = mul(probeLayout.invTransform, float4(pos, 1.)).xyz; // [0, 1] unless out of bounds
    lp = clamp(lp, float3(0.), float3(0.99999)); // [0, 1)
    lp *= probeLayout.probeCounts - 1;

    float3 fac = fract(lp);
    fac = smoothstep(float3(0.0), float3(1.0), fac);
    uint3 id = uint3(lp);
    uint2 d = uint2(1, 0);

    float2 uv = DirectionToUV(rd);
    float c000 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.yyy, uv, depthTexture);
    float c001 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.yyx, uv, depthTexture);
    float c010 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.yxy, uv, depthTexture);
    float c011 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.yxx, uv, depthTexture);
    float c100 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.xyy, uv, depthTexture);
    float c101 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.xyx, uv, depthTexture);
    float c110 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.xxy, uv, depthTexture);
    float c111 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.xxx, uv, depthTexture);

    float c00 = lerp(c000, c001, fac.z);
    float c01 = lerp(c010, c011, fac.z);
    float c10 = lerp(c100, c101, fac.z);
    float c11 = lerp(c110, c111, fac.z);

    float c0 = lerp(c00, c01, fac.y);
    float c1 = lerp(c10, c11, fac.y);

    float c = lerp(c0, c1, fac.x);

    return 0.1*GetInterpolatedDepth(probeLayout.probeCounts, uint3(round(lp)), uv, depthTexture);//c000;//c*0.1;
}

float2 bl(float3 rd, float3 pos) {
    float3 lp = mul(probeLayout.invTransform, float4(pos, 1.)).xyz; // [0, 1] unless out of bounds
    lp = clamp(lp, float3(0.), float3(0.99999));                    // [0, 1)
    lp *= probeLayout.probeCounts - 1;

    float3 fac = fract(lp);
    fac = smoothstep(float3(0.0), float3(1.0), fac);
    uint3 id = uint3(lp);
    uint2 d = uint2(1, 0);

    float2 uv = DirectionToUV(rd);
    float c000 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.yyy, uv, depthTexture);
    float c001 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.yyx, uv, depthTexture);
    float c010 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.yxy, uv, depthTexture);
    float c011 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.yxx, uv, depthTexture);
    float c100 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.xyy, uv, depthTexture);
    float c101 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.xyx, uv, depthTexture);
    float c110 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.xxy, uv, depthTexture);
    float c111 = GetInterpolatedDepth(probeLayout.probeCounts, id + d.xxx, uv, depthTexture);

    float c00 = lerp(c000, c001, fac.z);
    float c01 = lerp(c010, c011, fac.z);
    float c10 = lerp(c100, c101, fac.z);
    float c11 = lerp(c110, c111, fac.z);

    float c0 = lerp(c00, c01, fac.y);
    float c1 = lerp(c10, c11, fac.y);

    float c = lerp(c0, c1, fac.x);

    return uv;//localUVToAtlasUV(probeLayout.probeCounts, uint3(round(lp)), uv);
}

[shader("fragment")]
float4 fragMain(FSInput o) : SV_Target
{
    float4x4 viewTranspose = transpose(ubo.view);
    float3 camPos = -mul(float3x3(viewTranspose), viewTranspose[3].xyz);
    float3 norm = normalize(o.norm);//normalize(o.worldPos); // for sphere for now, but get normals and test them soon

    return  float4(float3(SampleInterpolatedDepth(norm, o.worldPos)), 1.);

}