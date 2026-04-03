#include "probe-creator.h"
#include <iostream>
#include "helper/math.h"

// Assume no other dynamic offs in mat
void ProbeVolume::DrawDebugProbeVolume(WRenderPass* renderPass, const Mesh& probeMesh, const Material& mat, uint32_t setIndex) {
	uint32_t count = probeCounts.x * probeCounts.y * probeCounts.z;

	for (uint32_t i = 0; i < count; i++) {
		renderPass->EnqueueSetMaterial(mat, setIndex, { i });
		renderPass->EnqueueDraw(probeMesh);
	}
}

/// buf should exist but not have Create() run on it
vector<WBuffer>* ProbeVolume::CreateEntityListUBO(const VulkanReferences& ref) {
	uint32_t count = probeCounts.x * probeCounts.y * probeCounts.z;

	// Create entity structs
	vector<UEntity> entities(count);
	for (int k = 0; k < probeCounts.z; k++) {
		for (int j = 0; j < probeCounts.y; j++) {
			for (int i = 0; i < probeCounts.x; i++) {
				vec3 uv = vec3(i, j, k) / (vec3(probeCounts) - vec3(1));
				vec3 pos = vec3(transform * vec4(uv, 1));
				entities[k * probeCounts.x * probeCounts.y + j * probeCounts.x + i] = {
					.transform = glm::translate(mat4(1.0f), pos) * glm::scale(mat4(1.0f), vec3(0.25f))
				};
			}
		}
	}

	// Create aligned buffer
	vk::DeviceSize alignment = GetUniformAlignment<UEntity>(ref);
	probeEntityUBO.emplace_back();
	WBuffer* buf = &probeEntityUBO[0];
	buf->Create(ref, alignment * count, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible);
	char* bufData = static_cast<char*>(buf->MapMemory());

	// Copy to buffer
	for (int i = 0; i < count; i++) {
		memcpy(bufData + i * alignment, &entities[i], sizeof(UEntity));
	}

	return &probeEntityUBO;
}

struct UProbePosition {
	alignas(16) vec3 probePos;
};

struct UProbeLayout {
	alignas(64) mat4 invTransform;
	alignas(64) mat4 transform;
	alignas(16) uvec3 probeCounts;
};

struct PBakePassInfo {
	alignas(4) uint32_t currGroup;
	alignas(4) uint32_t currBake;
};

// TODO: all these params def annoying so not having it, need to have some struct to represent the world
void ProbeCreator::Create(const VulkanReferences* ref, WTexture* skybox, vector<WBuffer>* uniformBuffers, WTexture* testCubeMap, Mesh* testRoom, WTexture* testRoomTexture, WTexture* metallic, WTexture* roughness, WTexture* ao,
	uvec3 probeCounts, vec3 boundingBoxOrigin, vec3 boundingBoxSize) {
	this->ref = ref;

	// Create Scratch Buffer
	vector<float> zeroData(SCRATCH_BUFFER_SIZE, 0.0f);
	zeroBuffer.CreateDeviceLocalFromData(*ref, SCRATCH_BUFFER_SIZE, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, zeroData.data());
	shScratchBuffer.Create(*ref, SCRATCH_BUFFER_SIZE, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eDeviceLocal);
	ZeroOutScratchBuffer();

	// Create Compute Dispatcher
	computeDispatcher.Create(*ref);

	// Create Skybox Probe Baker
	vector skyShaParams = {
		ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eCompute},
		ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eCompute},
	};
	vector skyMatParams = {
		ShaderParameter::MParameter(ShaderParameter::USampler{.texture = skybox}),
		ShaderParameter::MParameter(ShaderParameter::UBuffer{.buffer = &shScratchBuffer}),
	};
	bakeSkyboxProbe.Create(*ref, "shaders/spherical-harmonics-sky.spv", skyShaParams, skyMatParams, uvec3(SQRT_THREADS_PER_GROUP, SQRT_THREADS_PER_GROUP, 1));

	// Create Skybox SH
	skyboxSh = mkU<WBuffer>();
	skyboxSh->Create(*ref, 27 * sizeof(float), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
	BakeAndSetSkyboxProbe();

	// Create Empty Probe Position UBO
	probePositionUBO.push_back(WBuffer());
	probePositionUBO[0].Create(*ref, ceilToNearest(sizeof(UProbePosition), 16), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	probePositionUBO[0].MapMemory();

	// Make Probe Volume
	probeVolume = mkU<ProbeVolume>();

	vk::DeviceSize probeCount = probeCounts.x * probeCounts.y * probeCounts.z;
	vk::DeviceSize envShSize = sizeof(float) * 28;
	probeVolume->shCoefficients.Create(*ref, probeCount * envShSize,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
	probeVolume->depthBuffer.Create(*ref, probeCount * sizeof(float) * 16 * 16 * 2,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Transform
	mat4 transform = glm::translate(mat4(1.0), boundingBoxOrigin) * glm::scale(mat4(1.0), boundingBoxSize) * glm::translate(mat4(1.0), vec3(-0.5));

	// Parameters
	probeVolume->probeCounts = probeCounts;
	probeVolume->transform = transform;
	probeVolume->invTransform = glm::inverse(transform);

	// Create Uniform
	probeVolume->probeLayoutUBO.push_back(WBuffer());
	probeVolume->probeLayoutUBO[0].Create(*ref, ceilToNearest(sizeof(UProbeLayout), 16), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	probeVolume->probeLayoutUBO[0].MapMemory();
	UProbeLayout uProbeLayout = {
		.invTransform = probeVolume->invTransform,
		.transform = probeVolume->transform,
		.probeCounts = probeVolume->probeCounts
	};
	memcpy(probeVolume->probeLayoutUBO[0].mappedMemory, &uProbeLayout, sizeof(UProbeLayout));

	// Create Environment Probe Baker
	vector envShaParams = {
		ShaderParameter::SParameter{.type = ShaderParameter::Type::UNIFORM, .visibility = vk::ShaderStageFlagBits::eCompute },
		ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eCompute },
		ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eCompute },
		ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eCompute },
		ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eCompute },
		ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eCompute },
		ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eCompute },
		ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eCompute },
		ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eCompute },
		ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eCompute },
		ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eCompute },
		ShaderParameter::SParameter{.type = ShaderParameter::Type::UNIFORM, .visibility = vk::ShaderStageFlagBits::eCompute },
	};
	vector envMatParams = {
		ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = uniformBuffers}),
		ShaderParameter::MParameter(ShaderParameter::USampler {.texture = testCubeMap}),
		ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &testRoom->vertexBuffer}),
		ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &testRoom->indexBuffer}),
		ShaderParameter::MParameter(ShaderParameter::USampler {.texture = testRoomTexture}),
		ShaderParameter::MParameter(ShaderParameter::USampler {.texture = metallic}),
		ShaderParameter::MParameter(ShaderParameter::USampler {.texture = roughness}),
		ShaderParameter::MParameter(ShaderParameter::USampler {.texture = ao}),
		ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = skyboxSh.get() }),
		ShaderParameter::MParameter(ShaderParameter::UBuffer{.buffer = &probeVolume->shCoefficients}),
		ShaderParameter::MParameter(ShaderParameter::UBuffer{.buffer = &probeVolume->depthBuffer}),
		ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = &probeVolume->probeLayoutUBO}),
	};
	bakeEnvironmentProbe.Create(*ref, "shaders/spherical-harmonics-env-prog.spv", envShaParams, envMatParams, uvec3(SQRT_THREADS_PER_GROUP, SQRT_THREADS_PER_GROUP, 1), true, sizeof(PBakePassInfo));

	// Bake
	BakeEnvironmentProbes(probeCounts, transform);
}

void ProbeCreator::AccumulateScratchIntoBuffer(WBuffer* buf, vk::DeviceSize dstOffset) {
	// Get Data
	WBuffer receiveBuffer;
	receiveBuffer.Create(*ref, SCRATCH_BUFFER_SIZE, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	receiveBuffer.CopyFrom(*ref, shScratchBuffer, SCRATCH_BUFFER_SIZE);

	// Accumulate Samples of each group
	vector<float> sh(27, 0.0f);
	float* data = (float*)receiveBuffer.MapMemory();
	for (int i = 0; i < SCRATCH_BUFFER_SIZE / sizeof(float); i++) {
		sh[i % 27] += data[i];
	}
	receiveBuffer.UnmapMemory();

	// Divide by monte carlo count
	for (int i = 0; i < 27; i++) {
		sh[i] /= (1.0f * SQRT_THREADS_PER_PASS * SQRT_THREADS_PER_PASS);
	}

	// Create return buffer
	buf->SetData(*ref, sh.data(), 27 * sizeof(float), dstOffset);
}

void ProbeCreator::ZeroOutScratchBuffer() {
	shScratchBuffer.CopyFrom(*ref, zeroBuffer, SCRATCH_BUFFER_SIZE);
}

void ProbeCreator::BakeEnvironmentProbes(glm::uvec3 probeCounts, mat4 transform) {
	assert(isSkyboxBaked);

	// Bake all probes
	uint32_t probeCount = probeCounts.x * probeCounts.y * probeCounts.z;
	uint32_t bakeCount = 4096; // TODO: need even more maybe or sh improvement
	uint32_t groupCount = ceilDiv(probeCount, GROUP_SIZE);
	std::cout << "Probe Count: " << probeCount << " Group Count: " << groupCount << std::endl;

	struct PBakePassInfo bakePassInfo;

	computeDispatcher.StartRecord(*ref);
	for (int i = 0; i < bakeCount; i++) {
		bakePassInfo.currBake = i;
		for (int g = 0; g < groupCount; g++) {
			bakePassInfo.currGroup = g;
			bakeEnvironmentProbe.EnqueuePushConstants(&computeDispatcher, &bakePassInfo);
			bakeEnvironmentProbe.EnqueueDispatch(&computeDispatcher, uvec3(SQRT_THREADS_PER_PASS, SQRT_THREADS_PER_PASS, 1));
		}
		bakeEnvironmentProbe.EnqueueComputeBarrier(&computeDispatcher, 
			vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead, 
			vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead);
	}
	computeDispatcher.FinishRecordSubmit(*ref, true);
}

WBuffer* ProbeCreator::BakeAndSetSkyboxProbe() {
	// Dispatch
	computeDispatcher.StartRecord(*ref);
	bakeSkyboxProbe.EnqueueDispatch(&computeDispatcher, uvec3(SQRT_THREADS_PER_PASS, SQRT_THREADS_PER_PASS, 1));
	computeDispatcher.FinishRecordSubmit(*ref, true);

	AccumulateScratchIntoBuffer(skyboxSh.get(), 0);
	isSkyboxBaked = true;

	ZeroOutScratchBuffer();

	return skyboxSh.get();
}