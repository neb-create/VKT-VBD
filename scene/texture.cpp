#include "texture.h"
#include "core/memory-helper.h"
#include "core/command-helper.h"
#include "scene/buffer.h"

void WTexture::Create(const VulkanReferences& ref, 
    uint32_t width, uint32_t height,
    vk::Format format,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties, 
    vk::ImageAspectFlags imageViewAspectFlags) {

    this->width = width;
    this->height = height;
    this->format = format;

    vk::ImageCreateInfo imageInfo = {
        .imageType = vk::ImageType::e2D,
        .format = format, // Same format as pixels in staging buffer
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1, // could be used to store sparsely, useful for 3D textures of voxel terrain with lots of air
        .tiling = tiling, // how to arrange texels Optimal = Implementation Dependent, Efficient while Linear = Linearly laid out rows (limited)
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };
    image = Image(ref.device, imageInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo = {
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = FindMemoryType(ref, memRequirements.memoryTypeBits, properties) };
    memory = vk::raii::DeviceMemory(ref.device, allocInfo);
    image.bindMemory(memory, 0);

    CreateImageView(ref, imageViewAspectFlags);
}

void WTexture::CopyFromBuffer(const VulkanReferences& ref, const WBuffer& buffer) {
    CommandBuffer cmd = BeginOneTimeCommands(ref);

    vk::BufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0, // 0 implies tightly packed
        .bufferImageHeight = 0,

        // where to copy the pixels to?
        .imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}
    };
    // Assume image has already been transitioned to optimal layout at this point
    cmd.copyBufferToImage(buffer.buffer, image, vk::ImageLayout::eTransferDstOptimal, { region });

    SubmitOneTimeCommands(ref, &cmd);
}

// Hardcoded src, dst access mask as well as src, dst stage (src is what must be done before barrier, and barrier must be done before dst)
void WTexture::TransitionImageLayoutHardcoded(const VulkanReferences& ref, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
    auto cmd = BeginOneTimeCommands(ref);

    vk::ImageMemoryBarrier barrier = {
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .image = image,
        .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
    };

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
        dstStage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else {
        throw std::invalid_argument("unsupported layout transition");
    }


    cmd.pipelineBarrier(srcStage, dstStage, {}, {}, nullptr, barrier);
    SubmitOneTimeCommands(ref, &cmd);
}

void WTexture::CreateSampler(const VulkanReferences& ref) {
    vk::SamplerCreateInfo samplerInfo = {
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,

        .mipmapMode = vk::SamplerMipmapMode::eLinear,


        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,

        .anisotropyEnable = vk::True, // One frag sampling from lots of texels, auto and nice
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

void WTexture::CreateImageView(const VulkanReferences& ref, vk::ImageAspectFlags aspectFlags) {
    vk::ImageViewCreateInfo viewInfo = {
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = { aspectFlags, 0, 1, 0, 1 }
    };
    view = ImageView(ref.device, viewInfo);
}