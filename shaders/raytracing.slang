module Raytracing;
import PBR;

public struct Vertex {
    float3 pos;
    float3 color;
    float2 uv;
    float3 norm;
};

public float3 SampleSkybox(float3 rd, SamplerCube skybox) {
    return skybox.Sample(rd).rgb;//.4 * float3(0.4, 0.5, 1.0);//10. * skybox.Sample(rd).rgb; // pow(skybox.Sample(rd).rgb,float3(2.2));  ;/
}

// For reflections, ro is position of first hitPos from camera (the frag pos), rd is reflected ray
public float4 SampleEnvironment(float3 ro, float3 rd,
        float4x4 model, // todo: inv transpose
        StructuredBuffer<Vertex> worldVertices,
        StructuredBuffer<uint32_t> worldIndices,
        Sampler2D worldTex,
        Sampler2D worldMetal,
        Sampler2D worldRoughness,
        Sampler2D worldAO,
        StructuredBuffer<float> skyboxSh,
        SamplerCube skybox
    ) {
    float minDist = 10000.;
    float2 hitUv;
    float3 hitPos, hitNorm;
    for (uint32_t i = 0; i < 4596; i += 3) { // TODO: dont hardcode
        Vertex vs[3] = Vertex[](worldVertices[worldIndices[i]], worldVertices[worldIndices[i + 1]], worldVertices[worldIndices[i + 2]]);
        float3 vp[3] = float3[](
            mul(model, float4(vs[0].pos, 1.)).xyz,
            mul(model, float4(vs[1].pos, 1.)).xyz,
            mul(model, float4(vs[2].pos, 1.)).xyz
        );
        // TODO: ( later should localize query to run bvh)
        float3 cl = cross(vp[2] - vp[0], vp[1] - vp[0]);
        if (length(cl) < 0.001) continue;
        float area = 0.5 * length(cl);
        float3 oNorm = normalize(cl);

        // Plane
        float dist = -dot(oNorm, ro - vp[0]) / dot(oNorm, rd);
        if (dist > 0.01 && dist < minDist) {
            float3 hitPnt = ro + rd * dist;
            float b2 = 0.5 * length(cross(hitPnt - vp[0], hitPnt - vp[1]));
            float b0 = 0.5 * length(cross(hitPnt - vp[1], hitPnt - vp[2]));
            float b1 = 0.5 * length(cross(hitPnt - vp[2], hitPnt - vp[0]));
            if (b0 + b1 + b2 <= area + 0.001) {
                minDist = dist;

                hitUv = (b0 * vs[0].uv + b1 * vs[1].uv + b2 * vs[2].uv) / area;
                hitPos = (b0 * vp[0] + b1 * vp[1] + b2 * vp[2]) / area;
                hitNorm = normalize(mul(model, float4(b0 * vs[0].norm + b1 * vs[1].norm + b2 * vs[2].norm, 0.)).xyz); // TODO: make inverse transpose
            }
        }
    }

    if (minDist < 10000.) {
        float3 albedo = textureGammaCorrect(worldTex.Sample(hitUv).rgb);
        float metallic = 0.;   // worldMetal.Sample(hitUv).r;
        float roughness = 0.2; // worldRoughness.Sample(hitUv).r;
        float ao = 0.;         // worldAO.Sample(hitUv).r;
        return float4(SamplePBRSky(normalize(ro - hitPos), hitPos, hitNorm, albedo, metallic, roughness, ao, skyboxSh), minDist);
    } else {
        return float4(SampleSkybox(rd, skybox), 10000.0);
    }
}