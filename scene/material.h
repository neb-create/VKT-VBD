#pragma once

#include "defines.h"
// #include "pipeline.h"

#include "scene/buffer.h"
#include "scene/texture.h"
#include "scene/shader-parameter.h"

class WPipeline;

using namespace glm;

class Material {
public:
	void Create(const WPipeline*, const VulkanReferences&, const vector<ShaderParameter::MParameter>& parameters, int duplicationCount = MAX_FRAMES_IN_FLIGHT);// WTexture&, WTexture&, WTexture&, WTexture&, array<WBuffer*, 2>& meshBuffers);
	vector<vk::raii::DescriptorSet> descriptorSets;
	const WPipeline* pipeline = nullptr;
private:
	friend class ShaderPipeline;
	vector<ShaderParameter::MParameter> params;
	void CreateDescriptorSets(const VulkanReferences&, const vector<ShaderParameter::MParameter>& parameters);// WTexture&, WTexture&, WTexture&, WTexture&, array<WBuffer*, 2>& meshBuffers);

	int duplicationCount = -1;
};

struct UniformBufferObject {
	alignas(4) float off;
	alignas(16) mat4 raytraceSceneModel; // TODO: remove
	alignas(16) mat4 view;
	alignas(16) mat4 proj;
};

struct UEntity {
	mat4 transform;
};