import probe_helper;
import math_helper;
import probe_visibility;

module SHSampling;

#define PI 3.141592

uint probeIDToOffset(uint3 probeCounts, uint3 id) {
    return 28 * (probeCounts.x * probeCounts.y * id.z + probeCounts.x * id.y + id.x);
}

// TODO: potentially put skybox SH into cubemap since it's one of a kind and is used a lot
public float3 SampleSh(float3 rd, StructuredBuffer<float> shCoefficients, uint bufferOffset) {

    // rd = normalize(rd);
    float cosCo1 = 1.; // PI;
    float cosCo2 = 1.; //(2. / 3.) * PI;
    float cosCo3 = 1.; // PI * .25;
    float mult = 4. * PI;// / (64.0 * 1024.);// / (128. * 128.); // TODO: dont hardcode the monte carlo count
    float3 c1 = mult * cosCo1 * float3(shCoefficients[0 + bufferOffset], shCoefficients[1 + bufferOffset], shCoefficients[2 + bufferOffset]);

    float3 c2 = mult * cosCo2 * float3(shCoefficients[3 + bufferOffset], shCoefficients[4 + bufferOffset], shCoefficients[5 + bufferOffset]);
    float3 c3 = mult * cosCo2 * float3(shCoefficients[6 + bufferOffset], shCoefficients[7 + bufferOffset], shCoefficients[8 + bufferOffset]);
    float3 c4 = mult * cosCo2 * float3(shCoefficients[9 + bufferOffset], shCoefficients[10 + bufferOffset], shCoefficients[11 + bufferOffset]);

    float3 c5 = mult * cosCo3 * float3(shCoefficients[12 + bufferOffset], shCoefficients[13 + bufferOffset], shCoefficients[14 + bufferOffset]);
    float3 c6 = mult * cosCo3 * float3(shCoefficients[15 + bufferOffset], shCoefficients[16 + bufferOffset], shCoefficients[17 + bufferOffset]);
    float3 c7 = mult * cosCo3 * float3(shCoefficients[18 + bufferOffset], shCoefficients[19 + bufferOffset], shCoefficients[20 + bufferOffset]);
    float3 c8 = mult * cosCo3 * float3(shCoefficients[21 + bufferOffset], shCoefficients[22 + bufferOffset], shCoefficients[23 + bufferOffset]);
    float3 c9 = mult * cosCo3 * float3(shCoefficients[24 + bufferOffset], shCoefficients[25 + bufferOffset], shCoefficients[26 + bufferOffset]);

    return 
        c1 * 0.28209479 +
        c2 * 0.4886025119029199 * rd.y +
        c3 * 0.4886025119029199 * rd.z +
        c4 * 0.4886025119029199 * rd.x +
        c5 * 1.0925484305920792 * rd.x * rd.y +
        c6 * 1.0925484305920792 * rd.y * rd.z +
        c7 * 0.31539156525252005 * (3 * rd.z * rd.z - 1) +
        c8 * 1.0925484305920792 * rd.x * rd.z +
        c9 * 0.5462742152960396 * (rd.x * rd.x - rd.y * rd.y);
}

#define DEBUG_OUT_OF_BOUNDS 0

public float3 SampleInterpolatedSh(float3 norm, float3 pos, StructuredBuffer<float> shCoefficients, Sampler2D<float> depthTexture, ConstantBuffer<UProbeVolumeLayout> probeLayout) {
    float3 lp = mul(probeLayout.invTransform, float4(pos, 1.)).xyz; // [0, 1] unless out of bounds
    #if DEBUG_OUT_OF_BOUNDS
    bool outOfBounds = min(lp.x, min(lp.y, lp.z)) < 0. || max(lp.x, max(lp.y, lp.z)) >= 1.;
    #endif
    lp = clamp(lp, float3(0.), float3(0.99999));                    // [0, 1)
    lp *= probeLayout.probeCounts-1;

    float3 fac = fract(lp);
    fac = smoothstep(float3(0.0), float3(1.0), fac);
    uint3 id = uint3(lp);
    uint2 d = uint2(1, 0);

    const uint3 offsets[8] = {
        uint3(0, 0, 0),
        uint3(0, 0, 1),
        uint3(0, 1, 0),
        uint3(0, 1, 1),
        uint3(1, 0, 0),
        uint3(1, 0, 1),
        uint3(1, 1, 0),
        uint3(1, 1, 1)
    };

    float3 totalIrradiance = 0.0;
    float totalWeight = 0.0;

    for (int i = 0; i < 8; i++) {
        uint3 offset = offsets[i];
        float3 probePos = mul(probeLayout.transform, float4(float3(id + offset) / float3(probeLayout.probeCounts - 1), 1.)).xyz;
        float3 toProbe = probePos - pos;

        float distToProbe = length(toProbe);
        float3 dirToProbe = normalize(toProbe);

        // Sample
        float3 sampleIrradiance = SampleSh(norm, shCoefficients, probeIDToOffset(probeLayout.probeCounts, id + offset));

        // Trilinear
        float3 trilinearWeights = lerp(1.-fac, fac, float3(offset));
        float weight = trilinearWeights.x * trilinearWeights.y * trilinearWeights.z;

        // Backface
        // Offset prevents going to 0 when small details that are close to backfaces need light
        // weight *= sqr(max(1e-4, (dot(dirToProbe, norm) + 1.) * .5));// + 0.2; // From the Josa Paper TODO: proper citation

        // Depth
        float2 depths = GetInterpolatedDepthAndSqr(probeLayout.probeCounts, id + offset, DirectionToUV(-dirToProbe), depthTexture);
        float avgDepth = depths.x;
        float avgDepthSqr = depths.y;
        float variance = abs(avgDepthSqr - sqr(avgDepth)); // y abs
        float chebyshevWeight = variance / (variance + sqr(max(distToProbe - avgDepth, 0.))); // max shouldnt be necessary?
        chebyshevWeight = max(chebyshevWeight * chebyshevWeight * chebyshevWeight, 0.);
        weight *= distToProbe <= avgDepth ? 1.0 : 0.;//chebyshevWeight;

        // Accumulate
        totalIrradiance += weight * sampleIrradiance;
        totalWeight += weight;
    }

    // float3 c000 = SampleSh(rd, shCoefficients, probeIDToOffset(probeLayout.probeCounts, id + d.yyy));
    // float3 c001 = SampleSh(rd, shCoefficients, probeIDToOffset(probeLayout.probeCounts, id + d.yyx));
    // float3 c010 = SampleSh(rd, shCoefficients, probeIDToOffset(probeLayout.probeCounts, id + d.yxy));
    // float3 c011 = SampleSh(rd, shCoefficients, probeIDToOffset(probeLayout.probeCounts, id + d.yxx));
    // float3 c100 = SampleSh(rd, shCoefficients, probeIDToOffset(probeLayout.probeCounts, id + d.xyy));
    // float3 c101 = SampleSh(rd, shCoefficients, probeIDToOffset(probeLayout.probeCounts, id + d.xyx));
    // float3 c110 = SampleSh(rd, shCoefficients, probeIDToOffset(probeLayout.probeCounts, id + d.xxy));
    // float3 c111 = SampleSh(rd, shCoefficients, probeIDToOffset(probeLayout.probeCounts, id + d.xxx));

    // float3 c00 = lerp(c000, c001, fac.z);
    // float3 c01 = lerp(c010, c011, fac.z);
    // float3 c10 = lerp(c100, c101, fac.z);
    // float3 c11 = lerp(c110, c111, fac.z);

    // float3 c0 = lerp(c00, c01, fac.y);
    // float3 c1 = lerp(c10, c11, fac.y);

    // float3 c = lerp(c0, c1, fac.x);

    // #if DEBUG_OUT_OF_BOUNDS
    // if (outOfBounds) {
    //     c = float3(2., 0., 2.);
    // }
    // #endif

    return totalIrradiance / max(1e-5, totalWeight);//float3(id) * 0.1;
}