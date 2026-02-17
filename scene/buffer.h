#pragma once

#include "defines.h"

class WBuffer {
public:
	void Create(const VulkanReferences& ref, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
	void* MapMemory();
	void UnmapMemory();
	void CopyFrom(const VulkanReferences& ref, const WBuffer& src, vk::DeviceSize size = 0);

	void* mappedMemory;
	vk::raii::Buffer buffer = nullptr;
	vk::raii::DeviceMemory bufferMemory = nullptr;
private:
	
	vk::DeviceSize bufferSize;

	bool isMapped = false;
};