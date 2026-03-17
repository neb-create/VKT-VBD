#include "gi-manager.h"

#include "scene/compute-dispatcher.h"
#include "scene/shader-parameter.h"

#include "scene/material.h"

#include <iostream>

void GIManager::Test(WTexture* skybox) {
	// GenerateSHCoefficients(skybox);
}

void GIManager::GenerateSHCoefficients(WTexture* skybox) {
	vk::DeviceSize byteSize = sizeof(float) * 3 * 9;
	array<float, 27> zeroData;
	for (int i = 0; i < 27; i++) {
		zeroData[i] = 0.0f;
	}
	shCoefficients.CreateDeviceLocalFromData(*ref, byteSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc, zeroData.data());

	vector sParams = { // ATOMIC
		ShaderParameter::SParameter{.type = ShaderParameter::Type::SAMPLER, .visibility = vk::ShaderStageFlagBits::eCompute},
		ShaderParameter::SParameter{.type = ShaderParameter::Type::BUFFER, .visibility = vk::ShaderStageFlagBits::eCompute},
	};

	vector mParams = {
		ShaderParameter::MParameter(ShaderParameter::USampler{.texture = skybox}),
		ShaderParameter::MParameter(ShaderParameter::UBuffer{.buffer = &shCoefficients}),
	};

	ComputePipeline generateShader; // need better shader name like sh generator lol
	generateShader.Create(*ref, "shaders/spherical-harmonics.spv", sParams, mParams, uvec3(8, 8, 1));

	ComputeDispatcher dispatcher;
	dispatcher.Create(*ref);

	dispatcher.StartRecord(*ref);
	generateShader.EnqueueDispatch(&dispatcher, uvec3(128, 128, 1));
	dispatcher.FinishRecordSubmit(*ref);

	WBuffer receiveBuffer;
	receiveBuffer.Create(*ref, byteSize, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	receiveBuffer.CopyFrom(*ref, shCoefficients, byteSize);

	float* data = (float*)receiveBuffer.MapMemory();
	for (int i = 0; i < 27; i++) {
		std::cout << data[i] << std::endl;
	}
	receiveBuffer.UnmapMemory();
}