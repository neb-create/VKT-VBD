#pragma once

#include "defines.h"
#include "pipeline.h"

#include "scene/buffer.h"
#include "scene/texture.h"

using namespace glm;

class Material {
public:
	void Create(const ShaderPipeline*, const vk::DescriptorPool&, const VulkanReferences&, const vector<WBuffer>& uniformBuffers, WTexture&, WTexture&, WTexture&, WTexture&);
	vector<vk::raii::DescriptorSet> descriptorSets;
private:
	void CreateDescriptorSets(const vk::DescriptorPool& descriptorPool, const VulkanReferences& ref, const vector<WBuffer>& uniformBuffers, WTexture&, WTexture&, WTexture&, WTexture&);

	const ShaderPipeline* pipeline;
};

struct UniformBufferObject {
	alignas(4) float off;
	alignas(16) mat4 model;
	alignas(16) mat4 view;
	alignas(16) mat4 proj;
};