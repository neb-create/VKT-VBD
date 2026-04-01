#pragma once
#include "defines.h"
#include "helper/math.h"

uint32_t FindMemoryType(const VulkanReferences& ref, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

template <typename T>
vk::DeviceSize GetUniformAlignment(const VulkanReferences& ref) {
	return ceilToNearest(sizeof(T), ref.minUniformBufferOffsetAlignment);
}