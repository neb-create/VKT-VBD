import Raytracing;
import probe_visibility;
import probe_helper;

groupshared float3[64][9] sharedCoefficients;
groupshared float4[64] sharedDepthSamples;

struct UniformBuffer {
    float off;
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};

ConstantBuffer<UniformBuffer> ubo;
SamplerCube skybox;
StructuredBuffer<Vertex> worldVertices;
StructuredBuffer<uint32_t> worldIndices;
Sampler2D worldTex;
Sampler2D worldMetal;
Sampler2D worldRoughness;
Sampler2D worldAO;
StructuredBuffer<float> skySh;
RWStructuredBuffer<float> shCoefficients;
RWStructuredBuffer<float> depthBuffer;
ConstantBuffer<UProbeVolumeLayout> probeLayout;

struct PushConstants {
    uint32_t currGroup;
    uint32_t currBake;
}

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

#define PI 3.141592

float hash11(uint x) {
    x += (x << 10u);
    x ^= (x >> 6u);
    x += (x << 3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return fract(float(x) * 0.01);
}

float2 hash12(uint x) {
    return float2(hash11(x), hash11(x * 14 + 52));
}

float2 hash22(float2 p)
{
    float3 p3 = fract(float3(p.xyx) * float3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

// from ShaderToy https://www.shadertoy.com/view/4tXyWN
float rng(uint2 seed) {
    uint2 q = 1103515245U * ((seed >> 1U) ^ (seed.yx));
    uint n = 1103515245U * ((q.x) ^ (q.y >> 3U));
    return float(n) * (1.0 / float(0xffffffffU));
}

float rngi(int i) {
    return fract(8592.682 * sin(float(i) * 0.0325 * 32.301));
}

float3 uniformSphere(float2 xi) {
    float theta = xi.x * 2. * PI;

    float y = xi.y * 2. - 1.;
    float r = 1. - y * y;

    float3 v = float3(r * float2(cos(theta), sin(theta)), y).xzy;

    return normalize(v); // Should already be normalized but just in case
}

float3 probeIDToPos(uint id) {
    float3 uv = get3DProbeID(probeLayout.probeCounts, id) / float3(probeLayout.probeCounts - 1);
    float3 pos = mul(probeLayout.transform, float4(uv, 1.0)).xyz;
    return pos;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID, 
    uint3 threadIDWithinGroup3: SV_GroupThreadID,
    uint3 groupID3 : SV_GroupID) {

    // Get current probe
    uint threadIDWithinGroup = threadIDWithinGroup3.y * 8 + threadIDWithinGroup3.x; // TODO: assuming 8x8
    uint threadGroupID = groupID3.y * 32 + groupID3.x;                              // TODO: assuming 1024 total

    uint probeCount = probeLayout.probeCounts.x * probeLayout.probeCounts.y * probeLayout.probeCounts.z;
    uint probeToBake = pushConstants.currGroup * 1024 + threadGroupID; // TODO: assuming 1024
    if (probeToBake >= probeCount) return; // Can't loop back around since the entire probe set is being done in parallel
    float3 probePos = probeIDToPos(probeToBake);

    // Sample Li & Rd
    float3 rd = uniformSphere(float2(rngi(id.x - id.y + 525 + pushConstants.currGroup*23 - pushConstants.currBake*7), rngi(id.x + id.y + 525 - pushConstants.currGroup*15 + pushConstants.currBake*9)));
    float4 samp = SampleEnvironment(probePos, rd, ubo.model, worldVertices, worldIndices, worldTex, worldMetal, worldRoughness, worldAO, skySh, skybox);
    float3 li = samp.xyz;
    float depth = samp.w;

    // Updated shared depth sample
    sharedDepthSamples[threadIDWithinGroup] = float4(rd, depth);

    // Updated shared sh
    float cosCo1 = PI;
    float cosCo2 = (2. / 3.) * PI;
    float cosCo3 = PI * .25;
    cosCo1 *= cosCo1;
    cosCo2 *= cosCo2;
    cosCo3 *= cosCo3;

    sharedCoefficients[threadIDWithinGroup][0] = 0.28209479 * li * cosCo1;

    sharedCoefficients[threadIDWithinGroup][1] = 0.4886025119029199 * rd.y * li * cosCo2;
    sharedCoefficients[threadIDWithinGroup][2] = 0.4886025119029199 * rd.z * li * cosCo2;
    sharedCoefficients[threadIDWithinGroup][3] = 0.4886025119029199 * rd.x * li * cosCo2;

    sharedCoefficients[threadIDWithinGroup][4] = 1.0925484305920792 * rd.x * rd.y * li * cosCo3;
    sharedCoefficients[threadIDWithinGroup][5] = 1.0925484305920792 * rd.y * rd.z * li * cosCo3;
    sharedCoefficients[threadIDWithinGroup][6] = 0.31539156525252005 * (3. * rd.z * rd.z - 1.) * li * cosCo3;
    sharedCoefficients[threadIDWithinGroup][7] = 1.0925484305920792 * rd.x * rd.z * li;
    sharedCoefficients[threadIDWithinGroup][8] = 0.5462742152960396 * (rd.x * rd.x - rd.y * rd.y) * li * cosCo3;

    // Wait til all threads in group are done
    GroupMemoryBarrierWithGroupSync();

    // First thread will be accumulator into main sh
    if (threadIDWithinGroup == 0) {

        // Blending
        float srcWeight = shCoefficients[probeToBake * 28 + 27];
        float dstWeight = 1.0; // todo: slow history changing and temporal stuff for realtime updating?
        float totalWeight = srcWeight + dstWeight;
        float srcBlend = srcWeight / totalWeight; float dstBlend = dstWeight / totalWeight;
        shCoefficients[probeToBake * 28 + 27] = totalWeight;
        
        //
        for (int basis = 0; basis < 9; basis++) {
            float3 sum = 0.;
            for (int thread = 0; thread < 64; thread++) {
                sum += sharedCoefficients[thread][basis];
            }
            sum /= 64.0;

            if (srcBlend <= 0.00001) {
                // Don't even sample the buffer if it's trash
                shCoefficients[probeToBake * 28 + basis * 3 + 0] = sum.x;
                shCoefficients[probeToBake * 28 + basis * 3 + 1] = sum.y;
                shCoefficients[probeToBake * 28 + basis * 3 + 2] = sum.z;
            } else {
                shCoefficients[probeToBake * 28 + basis * 3 + 0] = shCoefficients[probeToBake * 28 + basis * 3 + 0] * srcBlend + sum.x * dstBlend;
                shCoefficients[probeToBake * 28 + basis * 3 + 1] = shCoefficients[probeToBake * 28 + basis * 3 + 1] * srcBlend + sum.y * dstBlend;
                shCoefficients[probeToBake * 28 + basis * 3 + 2] = shCoefficients[probeToBake * 28 + basis * 3 + 2] * srcBlend + sum.z * dstBlend;
            }
            
        }

        // Do depth
        for (int thread = 0; thread < 64; thread++) {
            float4 depthSample = sharedDepthSamples[thread];
            float3 rd = depthSample.xyz; float depth = min(4., depthSample.w); // TODO: temp fix for massive depth inflating nearby texels
            int2 texel = int2(16)+int2(DirectionToTexel(rd));

            // Splat 5x5
            const int r = 0;
            for (int y = -r; y <= r; y++) {
                for (int x = -r; x <= r; x++) {
                    int2 currTexel = (texel + int2(x, y)) % int2(16);
                    bool2 looped = currTexel != (texel - int2(16) + int2(x, y));
                    if ((!looped.x && looped.y) || (looped.x && !looped.y)) {
                        currTexel = 16 - 1 - currTexel;
                    }

                    float weight = pow(max(0., dot(rd, TexelToDirection(currTexel))), 16.);

                    uint ind = TexelToBufferIndices(probeToBake, currTexel);
                    depthBuffer[ind + 0] += depth * weight;
                    depthBuffer[ind + 1] += depth * depth * weight;
                    depthBuffer[ind + 2] += weight;
                }
            }
        }
    }
}