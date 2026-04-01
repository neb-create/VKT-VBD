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
	alignas(16) uvec3 probeCounts;
};

// TODO: all these params def annoying so not having it, need to have some struct to represent the world
void ProbeCreator::Create(const VulkanReferences* ref, WTexture* skybox, vector<WBuffer>* uniformBuffers, WTexture* testCubeMap, Mesh* testRoom, WTexture* testRoomTexture, WTexture* metallic, WTexture* roughness, WTexture* ao) {
	this->ref = ref;

	// Create Scratch Buffer
	vector<float> zeroData(SCRATCH_BUFFER_SIZE, 0.0f);
	zeroBuffer.CreateDeviceLocalFromData(*ref, SCRATCH_BUFFER_SIZE, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, zeroData.data());
	shScratchBuffer.Create(*ref, SCRATCH_BUFFER_SIZE, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eDeviceLocal);
	ZeroOutScratchBuffer();

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

	// Create Empty Skybox SH Buffer
	skyboxSh = mkU<WBuffer>();
	skyboxSh->Create(*ref, 27 * sizeof(float), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Create Empty Probe Position Buffer
	probePositionUBO.push_back(WBuffer());
	probePositionUBO[0].Create(*ref, ceilToNearest(sizeof(UProbePosition), 16), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	probePositionUBO[0].MapMemory();

	// Create Environment Probe Baker
	vector envShaParams = {
		ShaderParameter::SParameter{.type = ShaderParameter::Type::UNIFORM, .visibility = vk::ShaderStageFlagBits::eCompute },
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
	};
	vector envMatParams = {
		ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = &probePositionUBO}),
		ShaderParameter::MParameter(ShaderParameter::UUniform {.uniformBuffers = uniformBuffers}),
		ShaderParameter::MParameter(ShaderParameter::USampler {.texture = testCubeMap}),
		ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &testRoom->vertexBuffer}),
		ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = &testRoom->indexBuffer}),
		ShaderParameter::MParameter(ShaderParameter::USampler {.texture = testRoomTexture}),
		ShaderParameter::MParameter(ShaderParameter::USampler {.texture = metallic}),
		ShaderParameter::MParameter(ShaderParameter::USampler {.texture = roughness}),
		ShaderParameter::MParameter(ShaderParameter::USampler {.texture = ao}),
		ShaderParameter::MParameter(ShaderParameter::UBuffer {.buffer = skyboxSh.get() }),
		ShaderParameter::MParameter(ShaderParameter::UBuffer{.buffer = &shScratchBuffer}),
	};
	bakeEnvironmentProbe.Create(*ref, "shaders/spherical-harmonics-env.spv", envShaParams, envMatParams, uvec3(SQRT_THREADS_PER_GROUP, SQRT_THREADS_PER_GROUP, 1));

	// Create Compute Dispatcher
	computeDispatcher.Create(*ref);
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
		sh[i] /= (1.0f * SQRT_SAMPLE_COUNT * SQRT_SAMPLE_COUNT);
	}

	// Create return buffer
	buf->SetData(*ref, sh.data(), 27 * sizeof(float), dstOffset);
}

void ProbeCreator::ZeroOutScratchBuffer() {
	shScratchBuffer.CopyFrom(*ref, zeroBuffer, SCRATCH_BUFFER_SIZE);
}

// TODO: expand scratch buffer to bake multiple at once using the same scratch buffer
void ProbeCreator::BakeEnvironmentProbe(vec3 pos, WBuffer* shCoefficients, vk::DeviceSize offset) {
	// TODO: until we decouple compute material and pipeline (compute material either inherits or is straight up equal to material for now), just do skybox ignore pos
	// well actually we dont decouple until we do stuff in parallel, for now and for a while just vary some uniform buffer that contains position and do one at a time..
	// scratch buffer makes doing stuff in parallel annoying anyways, atomic so much easier
	
	// Must have skybox baked to bake env
	assert(isSkyboxBaked);

	// Update UBO
	UProbePosition probePosStruct = {
		.probePos = pos
	};
	memcpy(probePositionUBO[0].mappedMemory, &probePosStruct, sizeof(UProbePosition)); // Copy doesn't have to be 16-aligned, 4 byte padding can be trash

	// Dispatch
	computeDispatcher.StartRecord(*ref);
	bakeEnvironmentProbe.EnqueueDispatch(&computeDispatcher, uvec3(SQRT_SAMPLE_COUNT, SQRT_SAMPLE_COUNT, 1)); // TODO: would need atomics to easily do multiple probes at once
	computeDispatcher.FinishRecordSubmit(*ref, true);
	
	// Accumulate into it
	AccumulateScratchIntoBuffer(shCoefficients, offset);

	// Clear scratch
	ZeroOutScratchBuffer();
}

uPtr<ProbeVolume> ProbeCreator::BakeEnvironmentProbes(glm::uvec3 probeCounts, vec3 boundingBoxOrigin, vec3 boundingBoxSize) {
	assert(isSkyboxBaked);

	// Transform
	mat4 transform = glm::translate(mat4(1.0), boundingBoxOrigin) * glm::scale(mat4(1.0), boundingBoxSize) * glm::translate(mat4(1.0), vec3(-0.5));

	// Sizes
	vk::DeviceSize probeCount = probeCounts.x * probeCounts.y * probeCounts.z;
	vk::DeviceSize shSize = 27 * sizeof(float);

	// Create Volume
	uPtr<ProbeVolume> probeVolume = mkU<ProbeVolume>();

	// Buffer
	probeVolume->shCoefficients.Create(*ref, probeCount * shSize,
		vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

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
		.probeCounts = probeVolume->probeCounts
	};
	memcpy(probeVolume->probeLayoutUBO[0].mappedMemory, &uProbeLayout, sizeof(UProbeLayout));

	// Bake all probes
	for (int k = 0; k < probeCounts.z; k++) {
		for (int j = 0; j < probeCounts.y; j++) {
			for (int i = 0; i < probeCounts.x; i++) {
				vec3 uv = vec3(i, j, k) / (vec3(probeCounts)-vec3(1));
				vec3 pos = transform * vec4(uv, 1.0);

				vk::DeviceSize flattenedIndex = k * probeCounts.y * probeCounts.x + j * probeCounts.x + i;

				BakeEnvironmentProbe(pos, &probeVolume->shCoefficients, flattenedIndex * shSize);

				std::cout << "Baked Probe " << (flattenedIndex+1) << "/" << (probeCounts.x * probeCounts.y * probeCounts.z) << std::endl;
			}
		}
	}

	return std::move(probeVolume);
}

WBuffer* ProbeCreator::BakeAndSetSkyboxProbe() {
	// Dispatch
	computeDispatcher.StartRecord(*ref);
	bakeSkyboxProbe.EnqueueDispatch(&computeDispatcher, uvec3(SQRT_SAMPLE_COUNT, SQRT_SAMPLE_COUNT, 1));
	computeDispatcher.FinishRecordSubmit(*ref, true);

	AccumulateScratchIntoBuffer(skyboxSh.get(), 0);
	isSkyboxBaked = true;

	ZeroOutScratchBuffer();

	return skyboxSh.get();
}