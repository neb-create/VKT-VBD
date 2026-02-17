#include "memory-helper.h"

uint32_t FindMemoryType(const VulkanReferences& ref, uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    // Find what memory vertex buffer shoul use
    vk::PhysicalDeviceMemoryProperties memProperties = ref.physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if (
            (typeFilter & (1 << i)) && // is in our type filter
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties // has at least all props we want
            ) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type");
}
