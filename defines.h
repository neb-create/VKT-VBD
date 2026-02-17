#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#	include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN // GLFW auto-loads Vulkan header
#include <GLFW/glfw3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // Force projection matrix to have (0,1) depth instead of (-1,1)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// We need these for our hash functinos
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include "stb_image.h"
#include "tiny_obj_loader.h"

#define U32T(v) (static_cast<uint32_t>(v))

struct VulkanReferences {
	vk::raii::Device device = nullptr;
	vk::raii::PhysicalDevice physicalDevice = nullptr;

	vk::raii::CommandPool commandPool = nullptr;

	vk::raii::Queue graphicsQueue = nullptr;
};