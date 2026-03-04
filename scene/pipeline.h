#pragma once

#include "defines.h"
#include <string>
#include "scene/shader-parameter.h"
#include <array>
#include "scene/compute-dispatcher.h"

class Material;
using namespace std;
using namespace glm;

class WPipeline {
public:
    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
protected:
    void CreateDescriptorSetLayout(const VulkanReferences& ref, const vector<ShaderParameter::SParameter>& parameters);
    [[nodiscard]] static vk::raii::ShaderModule CreateShaderModule(const VulkanReferences& ref, const string& path);

    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;
};

class ShaderPipeline : public WPipeline {
public:
    void Create(const VulkanReferences& ref, const string& path, const vk::Format* colorFormats, const vk::Format& depthFormat, const vector<ShaderParameter::SParameter>& parameters);
    void Bind(const vk::raii::CommandBuffer& cmd) const;
    void BindDescriptorSets(const vk::raii::CommandBuffer& cmd, const vk::raii::DescriptorSet& descriptorSet) const; // Link bindings to our binding layout (WebGPU terminology)
    
};

class ComputePipeline : public WPipeline {
public:
    void Create(const VulkanReferences& ref, const string& path, const vector<ShaderParameter::SParameter>& parameters, const vector<ShaderParameter::MParameter>& parameterData, uvec3 workGroupSize);
    // void DispatchImmediate(bool waitForComplete = true, uvec3 totalThreadCount);
    void EnqueueDispatch(ComputeDispatcher*, uvec3 threadSize); // wait but can't do arb compute shaders in different cmd buffers maybe info on it or smth NVM i think its ALL GOOD binding n shit is a cmd buf command

private:
    // Full definition required for member variables that aren't uPtrs since size needs to be known
    uPtr<Material> computeMaterial = nullptr; // TODO DECOUPLE, basically just do same setup as shader material, idk if i need to make child computematerial class, dont do it if dont need to?
    uvec3 workGroupSize;

    bool isCreated = false;
};