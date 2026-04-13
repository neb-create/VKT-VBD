import Raytracing;
import PBR;
import sh_sampling;
import probe_helper;

struct UniformBuffer {
    float off;
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

struct UEntity {
    float4x4 transform;
};
ConstantBuffer<UEntity> uTransform;

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
    float4 worldPos = float4(objPos, 1.);// NO MULT RN mul(uTransform.transform, float4(objPos, 1.0));
    vOut.worldPos = worldPos.xyz;
    vOut.position = mul(ubo.proj, mul(ubo.view, worldPos));
    vOut.color = vIn.color;
    vOut.uv = vIn.uv;
    vOut.norm = vIn.norm; // NO MULT RN mul(uTransform.transform, float4(vIn.norm, 0.0)).xyz; // make do opposite scale (inv trans)
    return vOut;
}

[shader("fragment")]
float4 fragMain(FSInput o) : SV_Target
{
    float4x4 viewTranspose = transpose(ubo.view);
    float3 camPos = -mul(float3x3(viewTranspose), viewTranspose[3].xyz);
    float3 norm = normalize(o.norm);//normalize(o.worldPos); // for sphere for now, but get normals and test them soon

    return float4(norm*0.5+0.5, 1.);

}