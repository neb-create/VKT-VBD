import Raytracing;
import PBR;

struct UniformBuffer {
    float off;
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

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
    float4 worldPos = mul(ubo.model, float4(objPos, 1.0));
    vOut.worldPos = worldPos.xyz;
    vOut.position = mul(ubo.proj, mul(ubo.view, worldPos));
    vOut.color = vIn.color;
    vOut.uv = vIn.uv;
    vOut.norm = mul(ubo.model, float4(vIn.norm, 0.0)).xyz; // make do opposite scale (inv trans)
    return vOut;
}

Sampler2D texSampler;
Sampler2D metallicSampler;
Sampler2D roughnessSampler;
Sampler2D aoSampler;

StructuredBuffer<Vertex> vertices;
StructuredBuffer<uint32_t> indices;

SamplerCube skybox;
StructuredBuffer<float> shCoefficients;

[shader("fragment")]
float4 fragMain(FSInput o) : SV_Target
{
    float4x4 viewTranspose = transpose(ubo.view);
    float3 camPos = -mul(float3x3(viewTranspose), viewTranspose[3].xyz);
    float3 norm = normalize(o.norm);//normalize(o.worldPos); // for sphere for now, but get normals and test them soon

    const float3 albedo = textureGammaCorrect(texSampler.Sample(o.uv).rgb); // float3(1.);// // float3(1.0,0.,0.);
    const float metallic = 0.;        // metallicSampler.Sample(o.uv).r; // 1.0; // 0.5
    const float roughness = 0.2;//roughnessSampler.Sample(o.uv).r;                // 0.1;
    const float ambientOcclusion = 0.;//aoSampler.Sample(o.uv).r;//0.;

    float3 Lo = float3(0.);
    Lo += 0.03 * ambientOcclusion * albedo; // Ambient Light

    float3 wo = normalize(camPos - o.worldPos);

    float3 col = gammaCorrect(reinhardOp(SamplePBR(wo, o.worldPos+norm*0.01, norm, albedo, metallic, roughness, ambientOcclusion, shCoefficients)));

    return float4(col, 1.);

}