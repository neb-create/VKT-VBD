#pragma once

#include "defines.h"
#include "scene/buffer.h"
#include "scene/compute-dispatcher.h"
#include "scene/pipeline.h"
#include "scene/mesh.h"

using namespace glm;

struct Probe {
	WBuffer shCoefficients;
	// TODO: octahedral depth map here
}; // TODO: use

class ProbeCreator {
public:
	void Create(const VulkanReferences*, WTexture* skybox, vector<WBuffer>* uniformBuffers, WTexture* testCubeMap, Mesh* testRoom, WTexture* testRoomTexture, WTexture* metallic, WTexture* roughness, WTexture* ao);
	uPtr<WBuffer> BakeEnvironmentProbe(vec3 pos);
	WBuffer* BakeAndSetSkyboxProbe();
private:
	void AccumulateScratchIntoBuffer(WBuffer* buf);
	void ZeroOutScratchBuffer();

	const VulkanReferences* ref;

	WBuffer shScratchBuffer;
	WBuffer zeroBuffer;

	uPtr<WBuffer> skyboxSh;
	bool isSkyboxBaked = false;

	ComputePipeline bakeEnvironmentProbe;
	ComputePipeline bakeSkyboxProbe;

	ComputeDispatcher computeDispatcher;

	vector<WBuffer> probePositionUBO; // shouldn't have to do vector..
	// WBuffer probePositionUBO;
};

// need to change spherical harmonic shader (how to flatten group id & thread id) as well as sampling shader (how much to divide sh sample, or just do it when cpu accuming) if you change this
constexpr uint32_t SQRT_THREAD_COUNT = 128;
constexpr uint32_t SQRT_THREADS_PER_GROUP = 8;

constexpr uint32_t GROUP_COUNT = (SQRT_THREAD_COUNT / SQRT_THREADS_PER_GROUP) * (SQRT_THREAD_COUNT / SQRT_THREADS_PER_GROUP);
constexpr vk::DeviceSize SCRATCH_BUFFER_SIZE = GROUP_COUNT * sizeof(float) * 3 * 9; // Each group gets its own SH, non-atomic