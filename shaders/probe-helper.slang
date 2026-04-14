module ProbeHelper;

public struct UProbeVolumeLayout {
    public float4x4 invTransform;
    public float4x4 transform;
    public uint3 probeCounts;
};

public uint get1DProbeID(uint3 probeCounts, uint3 id) {
    return (probeCounts.x * probeCounts.y * id.z + probeCounts.x * id.y + id.x);
}

public uint3 get3DProbeID(uint3 probeCounts, uint id) {
    return uint3(
        id % probeCounts.x,
        (id / probeCounts.x) % probeCounts.y,
        (id / (probeCounts.x * probeCounts.y))
    );
}

public uint tileIDToProbeID(uint3 probeCounts, uint2 tileID) {
    return get1DProbeID(probeCounts, uint3(tileID.x % probeCounts.x, tileID.x / probeCounts.x, tileID.y));
}

public float2 localUVToAtlasUV(uint3 probeCounts, uint3 probeID3, float2 uv) {
    int2 tileID = uint2(probeCounts.x * probeID3.y + probeID3.x, probeID3.z);

    // 16x16 spanning square to 18x18 with gutters
    float2 p = uv * 2. - 1.;
    float2 uv18 = (1.0 + 8. + p*8.) / float2(18.0);

    float2 atlasUV = (float2(tileID) + uv18) / float2(probeCounts.x * probeCounts.y, probeCounts.z); // conversion
    return atlasUV;
}