#include "texture.h"
#include "core/memory-helper.h"
#include "core/command-helper.h"
#include "scene/buffer.h"
#include <iostream>

void WTexture::Create(const VulkanReferences& ref, 
    uint32_t width, uint32_t height,
    vk::Format format,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties, 
    vk::ImageAspectFlags imageViewAspectFlags, uint32_t arrayLayerCount, bool cubeMap) {

    if (isCreated) {
        std::cerr << "Texture " << this << " was already created but is being recreated." << std::endl;
    }// assert(!isCreated);
    isCreated = true;

    this->width = width;
    this->height = height;
    this->format = format;
    this->arrayLayerCount = arrayLayerCount;
    this->isCubeMap = cubeMap;

    vk::ImageCreateInfo imageInfo = {
        .imageType = vk::ImageType::e2D,
        .format = format, // Same format as pixels in staging buffer
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = arrayLayerCount,
        .samples = vk::SampleCountFlagBits::e1, // could be used to store sparsely, useful for 3D textures of voxel terrain with lots of air
        .tiling = tiling, // how to arrange texels Optimal = Implementation Dependent, Efficient while Linear = Linearly laid out rows (limited)
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive,
    };
    if (cubeMap) {
        imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    }
    image = Image(ref.device, imageInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo = {
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = FindMemoryType(ref, memRequirements.memoryTypeBits, properties) };
    memory = vk::raii::DeviceMemory(ref.device, allocInfo);
    image.bindMemory(memory, 0);

    if (cubeMap) {
        CreateMainCubeMapImageView(ref, imageViewAspectFlags);
    }
    else {
        CreateMainImageView(ref, imageViewAspectFlags);
    }
}

void WTexture::CreateFromFile(const VulkanReferences& ref, const std::string& path, 
    vk::Format format,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties, vk::ImageAspectFlags imageViewAspectFlags, vk::ImageLayout targetLayout) 
{
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path.c_str(),
        &texWidth, &texHeight, &texChannels,
        STBI_rgb_alpha); // Forces loading alpha channel even if one doesnt exist
    vk::DeviceSize imageByteSize = texWidth * texHeight * 4;

    if (!pixels) {
        throw std::runtime_error("failed to load texture image");
    }

    // Staging to get the actual data closer to GPU (which we cant directly write to ig)
    WBuffer stagingBuffer;
    stagingBuffer.Create(ref, imageByteSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    stagingBuffer.MapMemory();
    memcpy(stagingBuffer.mappedMemory, pixels, imageByteSize);
    stagingBuffer.UnmapMemory();

    stbi_image_free(pixels);
    pixels = nullptr;

    // We want to copy from the image staging buffer to an image (not just a buffer)
    Create(ref, texWidth, texHeight, format, tiling, usage, properties, imageViewAspectFlags);

    // We need to transition this image through multiple layouts
    // Undefined -> Optimized for Receiving Data -> Optimized for Shader Reading
    TransitionImageLayoutHardcoded(ref, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    CopyFromBuffer(ref, stagingBuffer);
    TransitionImageLayoutHardcoded(ref, vk::ImageLayout::eTransferDstOptimal, targetLayout);

    CreateSampler(ref);

}

void WTexture::CreateCubeMapFromFiles(const VulkanReferences& ref, std::array<std::string, 6> paths, 
    vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::ImageAspectFlags imageViewAspectFlags, vk::ImageLayout targetLayout) {

    // Load Pixel Data
    std::array<stbi_uc*, 6> pixelArrays;
    int texWidth = -1, texHeight = -1, texChannels;
    for (int i = 0; i < 6; i++) {
        int ptw = texWidth; int pth = texHeight;
        pixelArrays[i] = stbi_load(paths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        assert(i == 0 || (texWidth == ptw && texHeight == pth)); // All images should be same

        if (!pixelArrays[i]) {
            throw std::runtime_error("failed to load texture image");
        }
    }
    vk::DeviceSize bytesPerLayer = texWidth * texHeight * 4;

    // Put Pixel Data into Buffer
    WBuffer stagingBuffer;
    stagingBuffer.Create(ref, 6*bytesPerLayer,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    stagingBuffer.MapMemory();
    for (int i = 0; i < 6; i++) {
        memcpy(static_cast<stbi_uc*>(stagingBuffer.mappedMemory) + i * bytesPerLayer, pixelArrays[i], bytesPerLayer);
    }
    stagingBuffer.UnmapMemory();

    // Free CPU Pixel Data
    for (int i = 0; i < 6; i++) {
        stbi_image_free(pixelArrays[i]);
        pixelArrays[i] = nullptr;
    }

    // Create Image
    Create(ref, texWidth, texHeight, format, tiling, usage, properties, imageViewAspectFlags, 6, true);

    // Transition to Optimal Transferring Layout, then copy from buffer, then transition to optimal shader reading layout
    TransitionImageLayoutHardcoded(ref, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    for (int i = 0; i < 6; i++) {
        CopyFromBuffer(ref, stagingBuffer, bytesPerLayer * i, i);
    }
    TransitionImageLayoutHardcoded(ref, vk::ImageLayout::eTransferDstOptimal, targetLayout);

    CreateSampler(ref);
}

void WTexture::CopyFromBuffer(const VulkanReferences& ref, const WBuffer& buffer, vk::DeviceSize bufferOffset, uint32_t arrayLayer) {
    CommandBuffer cmd = BeginOneTimeCommands(ref);

    vk::BufferImageCopy region = {
        .bufferOffset = bufferOffset,
        .bufferRowLength = 0, // 0 implies tightly packed
        .bufferImageHeight = 0,

        // where to copy the pixels to?
        .imageSubresource = { 
            .aspectMask = vk::ImageAspectFlagBits::eColor, 
            .mipLevel = 0, 
            .baseArrayLayer = arrayLayer, 
            .layerCount = 1 
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}
    };
    // Assume image has already been transitioned to optimal layout at this point
    cmd.copyBufferToImage(buffer.buffer, image, vk::ImageLayout::eTransferDstOptimal, { region });

    SubmitOneTimeCommands(ref, &cmd);
}

// images can be in different layouts at different times
// depending on what we're using the img for
// presenting has a diff layout than rendering (for optimization sake)
void WTexture::TransitionImageLayout(
    vk::raii::CommandBuffer& commandBuffer,
    vk::Image image,

    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::AccessFlags2 srcAccessMask,
    vk::AccessFlags2 dstAccessMask,
    vk::PipelineStageFlags2 srcStageMask,
    vk::PipelineStageFlags2 dstStageMask,

    vk::ImageAspectFlags imageAspectFlags,

    uint32_t arrayLayerCount
) {
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = imageAspectFlags,
            .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = arrayLayerCount
        }
    };
    vk::DependencyInfo dependencyInfo = {
        .dependencyFlags = {},
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };
    commandBuffer.pipelineBarrier2(dependencyInfo);
}

// Hardcoded src, dst access mask as well as src, dst stage (src is what must be done before barrier, and barrier must be done before dst)
// TODO: ASSUMING COLOR BAD FOR DEPTH TEX
void WTexture::StaticTransitionImageLayoutHardcodedEnqueue(CommandBuffer* cmd, const VulkanReferences& ref, vk::Image img, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t arrLayerCount) {
    vk::ImageMemoryBarrier barrier = {
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .image = img,
        .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, arrLayerCount }
    };

    // TODO: Can we just make src a func of old and dst a func of new?
    // 
    // SourceStage which pipeline stages must happen before barrier
    // DestinationStage which pipeline stage waits on barrier
    // eByRegion means barrier is a per region condition
    vk::PipelineStageFlags srcStage, dstStage;
    // Hardcode layout transitions
    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        // Undefined -> Transfer Destination
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        srcStage = vk::PipelineStageFlagBits::eTopOfPipe; // No waiting needed, earliest possible stage to wait on
        dstStage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        // Transfer Destination -> Shader Reading
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        srcStage = vk::PipelineStageFlagBits::eTransfer;
        dstStage = vk::PipelineStageFlagBits::eFragmentShader; // TODO: ACCOUNT FOR COMPUTE SHADER, this layout transition could be called for a compute shader too
    }
    else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eColorAttachmentOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eNone;
        barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    }
    else if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dstStage = vk::PipelineStageFlagBits::eFragmentShader; // TODO: ACCOUNT FOR COMPUTE SHADER, like just run an undefined to compute shader transition or smth or have a isCompute bool idk prolly not that big a deal either way but COULD cause an error technically
    }
    else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eColorAttachmentOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        srcStage = vk::PipelineStageFlagBits::eTransfer;
        dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    }
    else if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal && newLayout == vk::ImageLayout::ePresentSrcKHR) {
        barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        barrier.dstAccessMask = {};

        srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dstStage = vk::PipelineStageFlagBits::eBottomOfPipe;
    }
    else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eGeneral) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

        srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage = vk::PipelineStageFlagBits::eComputeShader;
    }
    else if (oldLayout == vk::ImageLayout::eGeneral && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        srcStage = vk::PipelineStageFlagBits::eComputeShader;
        dstStage = vk::PipelineStageFlagBits::eFragmentShader; // TODO: ACCOUNT FOR COMPUTE SHADER, like just run an undefined to compute shader transition or smth or have a isCompute bool idk prolly not that big a deal either way but COULD cause an error technically
    } else {
        throw std::invalid_argument("unsupported layout transition");
    }

    cmd->pipelineBarrier(srcStage, dstStage, {}, {}, nullptr, barrier);
}

void WTexture::TransitionImageLayoutHardcodedEnqueue(CommandBuffer* cmd, const VulkanReferences& ref, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
    StaticTransitionImageLayoutHardcodedEnqueue(cmd, ref, image, oldLayout, newLayout, arrayLayerCount);
}

void WTexture::TransitionImageLayoutHardcoded(const VulkanReferences& ref, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
    auto cmd = BeginOneTimeCommands(ref);
    TransitionImageLayoutHardcodedEnqueue(&cmd, ref, oldLayout, newLayout);
    SubmitOneTimeCommands(ref, &cmd);
}

void WTexture::CreateSampler(const VulkanReferences& ref) {
    assert(!hasSampler);
    vk::SamplerCreateInfo samplerInfo = {
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,

        .mipmapMode = vk::SamplerMipmapMode::eLinear,


        .addressModeU = vk::SamplerAddressMode::eRepeat, // add choice
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat, // break?

        .anisotropyEnable = vk::False, // One frag sampling from lots of texels, auto and nice
        .maxAnisotropy = ref.physicalDevice.getProperties().limits.maxSamplerAnisotropy,

        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eAlways,

        .borderColor = vk::BorderColor::eIntOpaqueBlack, // Only matters with clamp to border addressing mode
        .unnormalizedCoordinates = vk::False, // Use texDim for sampling or (0,1)


    };
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    // Sampler can be applied to any image
    sampler = Sampler(ref.device, samplerInfo);

    hasSampler = true;
}

void WTexture::CreateMainImageView(const VulkanReferences& ref, vk::ImageAspectFlags aspectFlags) {
    vk::ImageViewCreateInfo viewInfo = {
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = { .aspectMask = aspectFlags, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
    };
    view = ImageView(ref.device, viewInfo);
}

// TODO: make cubemap texture a child and override the createimageview func
void WTexture::CreateMainCubeMapImageView(const VulkanReferences& ref, vk::ImageAspectFlags aspectFlags) {
    vk::ImageViewCreateInfo viewInfo = {
        .image = image,
        .viewType = vk::ImageViewType::eCube,
        .format = format,
        .subresourceRange = { aspectFlags, 0, 1, 0, 6 }
    };
    view = ImageView(ref.device, viewInfo);
}

ImageView WTexture::CreateImageView(const VulkanReferences& ref, uint32_t arrayLayer, vk::ImageAspectFlags aspectFlags) {
    vk::ImageViewCreateInfo viewInfo = {
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {.aspectMask = aspectFlags, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = arrayLayer, .layerCount = 1 }
    };
    return ImageView(ref.device, viewInfo);
}