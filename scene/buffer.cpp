#include "scene/buffer.h"
#include "core/command-helper.h"
#include "core/memory-helper.h"

void WBuffer::Create(const VulkanReferences& ref, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) {
    bufferSize = size;

    vk::BufferCreateInfo bufferInfo{ .size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive };
    buffer = vk::raii::Buffer(ref.device, bufferInfo);

    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{ .allocationSize = memRequirements.size, .memoryTypeIndex = FindMemoryType(ref, memRequirements.memoryTypeBits, properties) };
    bufferMemory = vk::raii::DeviceMemory(ref.device, allocInfo);
    buffer.bindMemory(*bufferMemory, 0); // 0 is offset within memory region, nonzero needs divisible by memRequirements.alignment
    // hostCoherence ensures CPU memory = GPU memory so dont need to explicitly time this
    // GPU data guaranteed to be there by next queueSubmit
}

void* WBuffer::MapMemory() {
    assert(!isMapped);
    isMapped = true;
    mappedMemory = bufferMemory.mapMemory(0, bufferSize);
    return mappedMemory;
}
void WBuffer::UnmapMemory() {
    assert(isMapped);
    isMapped = false;
    bufferMemory.unmapMemory();
    mappedMemory = nullptr;
}

void WBuffer::CopyFrom(const VulkanReferences& ref, const WBuffer& src, vk::DeviceSize size) {
    if (size == 0) {
        size = bufferSize;
    }
    auto cmd = BeginOneTimeCommands(ref);
    cmd.copyBuffer(src.buffer, buffer, vk::BufferCopy(0, 0, size)); // From where to where
    SubmitOneTimeCommands(ref, &cmd);
}