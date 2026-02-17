#include "command-helper.h"

// For practical applications, you should combine multiple operations into single command buffer instead of beginning and ending with like one command and then waiting on idle queue (CopyBuffer..)
// Can try that
vk::raii::CommandBuffer BeginOneTimeCommands(const VulkanReferences& ref) {
    // Create a temp command buf, could create a different one for this exclusive purpose
    vk::CommandBufferAllocateInfo allocInfo{ .commandPool = ref.commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 };
    vk::raii::CommandBuffer cmd = std::move(ref.device.allocateCommandBuffers(allocInfo).front());
    // use temporary?
    cmd.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        });
    return cmd;
}

void SubmitOneTimeCommands(const VulkanReferences& ref, vk::raii::CommandBuffer* cmd) {
    cmd->end();
    // Graphics queue MUST support TRANSFER BIT
    ref.graphicsQueue.submit(vk::SubmitInfo{ .commandBufferCount = 1, .pCommandBuffers = &**cmd }, nullptr);
    ref.graphicsQueue.waitIdle(); // fence would be necessary with multiple transfers scheduled simultaneously
}