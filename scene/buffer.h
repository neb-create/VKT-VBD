#pragma once

#include "defines.h"

class WBuffer {
public:
	void Create(const VulkanReferences& ref, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
	void CreateDeviceLocalFromData(const VulkanReferences&, vk::DeviceSize, vk::BufferUsageFlags, void* data);

	void* MapMemory();
	void UnmapMemory();
	void CopyFrom(const VulkanReferences& ref, const WBuffer& src, vk::DeviceSize size = 0, vk::DeviceSize dstOffset = 0);
	void EnqueueCopyFrom(vk::raii::CommandBuffer*, const VulkanReferences& ref, const WBuffer& src, vk::DeviceSize size = 0);
	void SetData(const VulkanReferences&, void* data, vk::DeviceSize, vk::DeviceSize dstOffset = 0);

	void* mappedMemory;
	vk::raii::Buffer buffer = nullptr;
	vk::raii::DeviceMemory bufferMemory = nullptr;
	vk::DeviceSize bufferSize;
private:
	
	

	bool isMapped = false;
};