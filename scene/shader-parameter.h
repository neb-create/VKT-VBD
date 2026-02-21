#pragma once

#include "defines.h"
#include "scene/buffer.h"
#include "scene/texture.h"
#include <vector>

using namespace std;

namespace ShaderParameter {
	enum class Type {
		UNIFORM,
		SAMPLER,
		BUFFER
	};

	struct UUniform {
		// We should have a notion of uniforms and whether they're frame in flight duplicated like a uniform.h...
		const vector<WBuffer>* uniformBuffers;
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
			USampler sampler;
			UBuffer buffer;
		};

		inline MParameter(UUniform u) : type(Type::UNIFORM), uniform(u) {}
		inline MParameter(USampler s) : type(Type::SAMPLER), sampler(s) {}
		inline MParameter(UBuffer b) : type(Type::BUFFER), buffer(b) {}
	};

	struct SParameter {
		Type type;
		vk::ShaderStageFlagBits visibility;
		// For now Shader Parameters don't need any actual data besides type
	};
};