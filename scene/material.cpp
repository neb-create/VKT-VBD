#include "material.h"


using namespace std;

void Material::Create(const ShaderPipeline* pipeline, const vk::DescriptorPool& descriptorPool, const VulkanReferences& ref, const vector<WBuffer>& uniformBuffers, WTexture& testTexture) {
    this->pipeline = pipeline;

    // Will probably need to do other stuff later too
    CreateDescriptorSets(descriptorPool, ref, uniformBuffers, testTexture);
}

void Material::CreateDescriptorSets(const vk::DescriptorPool& descriptorPool, const VulkanReferences& ref, const vector<WBuffer>& uniformBuffers, WTexture& testTexture) {
    // Need to copy the layouts to make the descriptors
    vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *pipeline->descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocateInfo = {
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()
    };

    // Allocate
    descriptorSets.clear();
    descriptorSets = ref.device.allocateDescriptorSets(allocateInfo);
    assert(descriptorSets.size() == MAX_FRAMES_IN_FLIGHT);

    // Configure descriptor sets
    for (size_t i = 0; i < layouts.size(); i++) {
        // Descriptors that use buffers are configured with DescriptorBufferInfo
        vk::DescriptorBufferInfo bufferInfo = {
            .buffer = uniformBuffers[i].buffer,
            .offset = 0,
            .range = sizeof(UniformBufferObject)
        };
        vk::DescriptorImageInfo imageInfo = {
            .sampler = testTexture.GetSampler(),
            .imageView = testTexture.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };
        std::array descriptorWrites = {
            vk::WriteDescriptorSet {
                .dstSet = descriptorSets[i], // descriptor set to update
                .dstBinding = 0, // binding from beginning of CreateDescriptorSetLayout
                .dstArrayElement = 0, // descriptors can be arrays
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &bufferInfo // for buffer data, pImageInfo would be used for image data
            },

            vk::WriteDescriptorSet{
                .dstSet = descriptorSets[i],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &imageInfo
            }
        };

        ref.device.updateDescriptorSets(descriptorWrites, {});
    }
}