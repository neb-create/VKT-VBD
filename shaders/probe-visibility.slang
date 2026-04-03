module ProbeVisibility;

struct UProbeVolumeLayout {
    float4x4 invTransform;
    float4x4 transform;
    uint3 probeCounts;
};

// Assuming 16x16
public uint2 DirectionToTexel(float3 dir) {
    float3 adir = abs(dir);
    dir = normalize(dir) / (adir.x + adir.y + adir.z);

    float2 uv = dir.xy;
    if (dir.z < 0.) {
        uv = float2(sign(uv.x), sign(uv.y)) * (1. - abs(uv));
    }
    uv = uv * 0.5 + 0.5;

    return uint2(16.0 * uv);
}

public float3 TexelToDirection(uint2 texel) {
    float2 uv = float2(texel) / 16.0;
    uv = uv * 2. - 1.;

    float h = 1. - abs(uv.x) - abs(uv.y);
    if (h < 0.) {
        uv.x = sign(uv.x) * (1. - abs(uv.y));
        uv.y = sign(uv.y) * (1. - abs(uv.x));
    }

    return normalize(float3(uv, h));
}

public uint TexelToBufferIndices(uint probeID, uint2 texel) {
    uint texelID = texel.y * 16 + texel.x;
    return probeID * 512 + texelID * 2; // 2 Indices: (depth, weight), will be 3 later for depth^2
}

public float GetNormalizedDepth(uint probeID, uint2 texel, StructuredBuffer<float> depthBuffer) {
    uint ind = TexelToBufferIndices(probeID, texel);
    return depthBuffer[ind + 0] / depthBuffer[ind + 1];
}