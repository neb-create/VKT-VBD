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

void WBuffer::CreateDeviceLocalFromData(const VulkanReferences& ref, vk::DeviceSize size, vk::BufferUsageFlags usage, void* data)
{
    assert((usage & vk::BufferUsageFlagBits::eTransferDst) == vk::BufferUsageFlagBits::eTransferDst); // To transfer data into it
    WBuffer stagingBuffer;
    stagingBuffer.Create(ref, size,
        vk::BufferUsageFlagBits::eTransferSrc, // Can be source of a transfer
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);

    // Fill Vertex Buffer w Data
    stagingBuffer.MapMemory(); // (0, bufSize) are offset and size; Map vertex buffer data to cpu memory
    memcpy(stagingBuffer.mappedMemory, data, size);
    stagingBuffer.UnmapMemory();

    Create(ref, size, usage,
        vk::MemoryPropertyFlagBits::eDeviceLocal // Device local, can't map memory directly
    );

    CopyFrom(ref, stagingBuffer);
    // Staging buffer will be cleaned up RAII
    // Staging allows us to use high performance memory for loading data
    // In practice, not good to do a separate allocation for every object, better to do one big one and split it up (VulkanMemoryAllocator library)
    // You should even go a step further, allocate a single vertex and index buffer for lots of things and use offsets to bindvertexbuffers to store lots of 3D objects
}

void WBuffer::SetData(const VulkanReferences& ref, void* data, vk::DeviceSize size, vk::DeviceSize dstOffset) {
    WBuffer stagingBuffer;
    stagingBuffer.Create(ref, size,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);

    // Fill Vertex Buffer w Data
    stagingBuffer.MapMemory(); // (0, bufSize) are offset and size; Map vertex buffer data to cpu memory
    memcpy(stagingBuffer.mappedMemory, data, size);
    stagingBuffer.UnmapMemory();
    CopyFrom(ref, stagingBuffer, size, dstOffset);
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

void WBuffer::EnqueueCopyFrom(vk::raii::CommandBuffer* cmd, const VulkanReferences& ref, const WBuffer& src, vk::DeviceSize size) {
    cmd->copyBuffer(src.buffer, buffer, vk::BufferCopy(0, 0, size)); // From where to where
}

void WBuffer::CopyFrom(const VulkanReferences& ref, const WBuffer& src, vk::DeviceSize size, vk::DeviceSize dstOffset) {
    if (size == 0) {
        size = bufferSize;
    }
    auto cmd = BeginOneTimeCommands(ref);
    cmd.copyBuffer(src.buffer, buffer, vk::BufferCopy(0, dstOffset, size)); // From where to where
    SubmitOneTimeCommands(ref, &cmd);
}