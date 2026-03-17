#include "probe-creator.h"
#include <iostream>

struct UProbePosition {
	alignas(16) vec3 probePos;
};

int ceilToNearest(int v, int m) {
	int f = v % m;
	int g = v / m;
	std::cout << (g * m + (f != 0 ? 1 : 0)) << std::endl;
	return g * m + (f != 0 ? 1 : 0);
}

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

void ProbeCreator::AccumulateScratchIntoBuffer(WBuffer* buf) {
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

	// Create return buffer
	buf->SetData(*ref, 27 * sizeof(float), sh.data());
}

void ProbeCreator::ZeroOutScratchBuffer() {
	shScratchBuffer.CopyFrom(*ref, zeroBuffer, SCRATCH_BUFFER_SIZE);
}

uPtr<WBuffer> ProbeCreator::BakeEnvironmentProbe(vec3 pos) {
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
	bakeEnvironmentProbe.EnqueueDispatch(&computeDispatcher, uvec3(SQRT_THREAD_COUNT, SQRT_THREAD_COUNT, 1)); // TODO: would need atomics to easily do multiple probes at once
	computeDispatcher.FinishRecordSubmit(*ref, true);

	// Create Buffer
	uPtr<WBuffer> sh = mkU<WBuffer>();
	sh->Create(*ref, 27 * sizeof(float), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
	
	// Accumulate into it
	AccumulateScratchIntoBuffer(sh.get());

	// Clear scratch
	ZeroOutScratchBuffer();

	return std::move(sh);
}

WBuffer* ProbeCreator::BakeAndSetSkyboxProbe() {
	// Dispatch
	computeDispatcher.StartRecord(*ref);
	bakeSkyboxProbe.EnqueueDispatch(&computeDispatcher, uvec3(SQRT_THREAD_COUNT, SQRT_THREAD_COUNT, 1));
	computeDispatcher.FinishRecordSubmit(*ref, true);

	AccumulateScratchIntoBuffer(skyboxSh.get());
	isSkyboxBaked = true;

	ZeroOutScratchBuffer();

	return skyboxSh.get();
}