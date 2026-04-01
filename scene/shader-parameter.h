#pragma once

#include "defines.h"
#include "scene/buffer.h"
#include "scene/texture.h"
#include <vector>

using namespace std;

namespace ShaderParameter {
	enum class Type {
		UNIFORM,
		DYNAMIC_UNIFORM,
		SAMPLER,
		BUFFER
	};

	struct UUniform {
		// We should have a notion of uniforms and whether they're frame in flight duplicated like a uniform.h...
		const vector<WBuffer>* uniformBuffers;
	};

	struct UDynamicUniform {
		const vector<WBuffer>* buffers;
		vk::DeviceSize singleObjectSize;
	};

	struct USampler {
		WTexture* texture;
	};

	struct UBuffer {
		WBuffer* buffer;
	};

	struct MParameter {
		Type type;
		union {
			UUniform uniform;
			UDynamicUniform dynamicUniform;
			USampler sampler;
			UBuffer buffer;
		};

		inline MParameter(UUniform u) : type(Type::UNIFORM), uniform(u) {}
		inline MParameter(UDynamicUniform u) : type(Type::DYNAMIC_UNIFORM), dynamicUniform(u) {}
		inline MParameter(USampler s) : type(Type::SAMPLER), sampler(s) {}
		inline MParameter(UBuffer b) : type(Type::BUFFER), buffer(b) {}

		// inline ~MParameter() {}
	};

	struct SParameter {
		Type type;
		vk::ShaderStageFlagBits visibility;
		// For now Shader Parameters don't need any actual data besides type
	};
};