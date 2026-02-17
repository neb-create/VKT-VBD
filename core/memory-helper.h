#pragma once
#include "defines.h"

uint32_t FindMemoryType(const VulkanReferences& ref, uint32_t typeFilter, vk::MemoryPropertyFlags properties);
