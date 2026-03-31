#pragma once

#include "defines.h"
#include "scene/buffer.h"
#include <string>
#include "glm/glm.hpp"

using namespace glm;
using namespace vk::raii;

class WTexture {
public:
    void CreateFromFile(const VulkanReferences& ref, const std::string& path,
        vk::Format format,
        vk::ImageTiling tiling = vk::ImageTiling::eOptimal,
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageAspectFlags imageViewAspectFlags = vk::ImageAspectFlagBits::eColor, vk::ImageLayout targetLayout = vk::ImageLayout::eShaderReadOnlyOptimal);
    void CreateCubeMapFromFiles(const VulkanReferences& ref, std::array<std::string, 6> paths,
        vk::Format format,
        vk::ImageTiling tiling = vk::ImageTiling::eOptimal,
        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal, vk::ImageAspectFlags imageViewAspectFlags = vk::ImageAspectFlagBits::eColor, vk::ImageLayout targetLayout = vk::ImageLayout::eShaderReadOnlyOptimal);
    void Create(const VulkanReferences& ref,
        uint32_t width, uint32_t height,
        vk::Format format,
        vk::ImageTiling tiling,
        vk::ImageUsageFlags usage,
        vk::MemoryPropertyFlags properties, vk::ImageAspectFlags imageViewAspectFlags = vk::ImageAspectFlagBits::eColor,
        uint32_t arrayLayerCount = 1, bool cubeMap = false);
    void CreateFromExternalImage(const VulkanReferences& ref, const Image&, const ImageView&, uvec2 dim);

    void CopyFromBuffer(const VulkanReferences& ref, const WBuffer& buffer, vk::DeviceSize bufferOffset = 0, uint32_t arrayLayer = 0);
    void TransitionImageLayoutHardcoded(const VulkanReferences& ref, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
    void TransitionImageLayoutHardcodedEnqueue(CommandBuffer*, const VulkanReferences& ref, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
    static void TransitionImageLayout(
        vk::raii::CommandBuffer& commandBuffer,
        vk::Image image,
        vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
        vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask,
        vk::ImageAspectFlags imageAspectFlags, uint32_t baseArrayLayers
    );
    void CreateSampler(const VulkanReferences& ref);

    ImageView CreateImageView(const VulkanReferences& ref, uint32_t arrayLayer, vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor);

    Image image = nullptr;
    DeviceMemory memory = nullptr;
    ImageView view = nullptr;
    uint32_t width, height;
    uint32_t arrayLayerCount;

    inline const Sampler& GetSampler() {
        assert(hasSampler);
        return sampler;
    }
private:
    void CreateMainImageView(const VulkanReferences& ref, vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor);
    void CreateMainCubeMapImageView(const VulkanReferences& ref, vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor);
    
    vk::Format format;

    Sampler sampler = nullptr;
    bool hasSampler = false;

    bool isCubeMap;
    bool isCreated = false;
};