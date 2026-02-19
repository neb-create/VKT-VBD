#pragma once

#include "defines.h"
#include <string>

using namespace std;

class ShaderPipeline {
public:
    void Create(const VulkanReferences& ref, const string& path, const vk::Format* colorFormats, const vk::Format& depthFormat);
    void Bind(const vk::raii::CommandBuffer& cmd);
    void BindDescriptorSets(const vk::raii::CommandBuffer& cmd, const vk::raii::DescriptorSet& descriptorSet); // Link bindings to our binding layout (WebGPU terminology)

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
private:
    [[nodiscard]] static vk::raii::ShaderModule CreateShaderModule(const VulkanReferences& ref, const string& path);
    void CreateDescriptorSetLayout(const VulkanReferences& ref);

    
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;
};