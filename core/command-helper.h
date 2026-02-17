#pragma once

#include "defines.h"

// For practical applications, you should combine multiple operations into single command buffer instead of beginning and ending with like one command and then waiting on idle queue (CopyBuffer..)
// Can try that
vk::raii::CommandBuffer BeginOneTimeCommands(const VulkanReferences& ref);

void SubmitOneTimeCommands(const VulkanReferences& ref, vk::raii::CommandBuffer* cmd);