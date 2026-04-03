#pragma once

#include "defines.h"
#include "scene/buffer.h"
#include "scene/compute-dispatcher.h"
#include "scene/pipeline.h"
#include "scene/mesh.h"
#include "scene/material.h"
#include "core/memory-helper.h"
#include "scene/render-pass.h"

using namespace glm;

struct ProbeVolume {
	WBuffer shCoefficients;
	glm::uvec3 probeCounts;
	glm::mat4 transform;
	glm::mat4 invTransform; // to (0, 1)
	vector<WBuffer> probeLayoutUBO;
	// TODO: octahedral depth map atlas here with gutter which will be filled in post process step
	WTexture octahedralDepthMap;

	vector<WBuffer> probeEntityUBO;

	void DrawDebugProbeVolume(WRenderPass*, const Mesh& probeMesh, const Material&, uint32_t setIndex);
	vector<WBuffer>* CreateEntityListUBO(const VulkanReferences& ref);
};

class ProbeCreator {
public:
	void Create(const VulkanReferences*, WTexture* skybox, vector<WBuffer>* uniformBuffers, WTexture* testCubeMap, Mesh* testRoom, WTexture* testRoomTexture, WTexture* metallic, WTexture* roughness, WTexture* ao,
		uvec3 probeCounts, vec3 boundingBoxOrigin, vec3 boundingBoxSize);
	uPtr<ProbeVolume> probeVolume;
private:
	void BakeEnvironmentProbes(glm::uvec3 probeCounts, mat4 transform);
	WBuffer* BakeAndSetSkyboxProbe();
	// void BakeEnvironmentProbe(vec3 pos, WBuffer* shCoefficients, vk::DeviceSize offset);
	void AccumulateScratchIntoBuffer(WBuffer* buf, vk::DeviceSize offset);
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
};

// need to change spherical harmonic shader (how to flatten group id & thread id) as well as sampling shader (how much to divide sh sample, or just do it when cpu accuming) if you change this
constexpr uint32_t SQRT_THREADS_PER_PASS = 256;
constexpr uint32_t SQRT_THREADS_PER_GROUP = 8;

constexpr uint32_t GROUP_SIZE = (SQRT_THREADS_PER_PASS / SQRT_THREADS_PER_GROUP) * (SQRT_THREADS_PER_PASS / SQRT_THREADS_PER_GROUP);
constexpr vk::DeviceSize SCRATCH_BUFFER_SIZE = GROUP_SIZE * sizeof(float) * 3 * 9; // Each group gets its own SH, non-atomic