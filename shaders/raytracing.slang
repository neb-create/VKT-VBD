module Raytracing;

// can have a scene data struct with skybox and triangles and materials n stuff? later
// right now environment is just skybox

public float3 SampleSkybox(float3 rd, SamplerCube skybox) {
    return skybox.Sample(rd).rgb;//pow(skybox.Sample(rd).rgb,float3(2.2));
}

float3 SampleEnvironment(float3 ro, float3 rd) {
    return float3(1., 0., 0.);
}