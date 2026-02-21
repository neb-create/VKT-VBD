#pragma once

#include "defines.h"
#include "pipeline.h"

#include "scene/buffer.h"
#include "scene/texture.h"
#include "scene/shader-parameter.h"

using namespace glm;

class Material {
public:
	void Create(const ShaderPipeline*, const vk::DescriptorPool&, const VulkanReferences&, vector<ShaderParameter::MParameter>& parameters);// WTexture&, WTexture&, WTexture&, WTexture&, array<WBuffer*, 2>& meshBuffers);
	vector<vk::raii::DescriptorSet> descriptorSets;
private:
	void CreateDescriptorSets(const vk::DescriptorPool& descriptorPool, const VulkanReferences& ref, vector<ShaderParameter::MParameter>& parameters);// WTexture&, WTexture&, WTexture&, WTexture&, array<WBuffer*, 2>& meshBuffers);

	const ShaderPipeline* pipeline;
};

struct UniformBufferObject {
	alignas(4) float off;
	alignas(16) mat4 model;
	alignas(16) mat4 view;
	alignas(16) mat4 proj;
};