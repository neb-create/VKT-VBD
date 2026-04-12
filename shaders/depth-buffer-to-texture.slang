import probe_visibility;
import probe_helper;

StructuredBuffer<float> inDepthBuffer;
[[vk::image_format("r32f")]] RWTexture2D<float> outDepthTexture;
ConstantBuffer<UProbeVolumeLayout> probeLayout;

int2 tryLoop(int2 lTexel) {
    int2 unloopedTexel = lTexel;
    lTexel = (lTexel + int2(16)) % int2(16);
    bool2 looped = lTexel != unloopedTexel;
    if ((looped.x && !looped.y) || (!looped.x && looped.y)) {
        lTexel = 15 - lTexel;
    }
    return lTexel;
}

bool eq2(int2 a, int2 b) {
    return a.x == b.x && a.y == b.y;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id: SV_DispatchThreadID) {
    if (id.x > probeLayout.probeCounts.x*probeLayout.probeCounts.y*18 || id.y > probeLayout.probeCounts.z*18) {
        return;
    }

    uint2 tileID = id.xy / int2(18);
    uint probeID = tileIDToProbeID(probeLayout.probeCounts, tileID);

    // if (
    //     !eq2(tryLoop(int2(-1, 1)), int2(0, 15 - 1)) ||
    //     !eq2(tryLoop(int2(13, 16)), int2(2, 15))
    // ) {
    //     outDepthTexture[id.xy] = 0.;
    //     return;
    // }

    int2 lTexel = (id.xy % int2(18)) - int2(1);
    int2 unloopedTexel = lTexel;
    lTexel = (lTexel + int2(16)) % int2(16);
    bool2 looped = lTexel != unloopedTexel;
    if ((looped.x && !looped.y) || (!looped.x && looped.y)) {
        lTexel = 15 - lTexel;
    }
    uint bufferInd = TexelToBufferIndices(probeID, lTexel);
    outDepthTexture[id.xy] = inDepthBuffer[bufferInd] / max(1e-5, inDepthBuffer[bufferInd + 1]);
}