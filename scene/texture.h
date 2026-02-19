#pragma once

#include "defines.h"
#include "scene/buffer.h"

using namespace vk::raii;

class WTexture {
public:
    void Create(const VulkanReferences& ref,
        uint32_t width, uint32_t height,
        vk::Format format,
        vk::ImageTiling tiling,
        vk::ImageUsageFlags usage,
        vk::MemoryPropertyFlags properties, vk::ImageAspectFlags imageViewAspectFlags = vk::ImageAspectFlagBits::eColor);

    void CopyFromBuffer(const VulkanReferences& ref, const WBuffer& buffer);
    void TransitionImageLayoutHardcoded(const VulkanReferences& ref, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
    void CreateSampler(const VulkanReferences& ref);

    Image image = nullptr;
    DeviceMemory memory = nullptr;
    ImageView view = nullptr;

    inline const Sampler& GetSampler() {
        assert(hasSampler);
        return sampler;
    }
private:
    void CreateImageView(const VulkanReferences& ref, vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor);

    uint32_t width, height;
    vk::Format format;

    Sampler sampler = nullptr;
    bool hasSampler = false;
};