#include "pipeline.h"
#include <array>
#include <vector>
#include "scene/resource-helper.h"
#include "scene/mesh.h"
#include "scene/material.h"
#include "scene/shader-parameter.h"
#include "helper/math.h"

#include <iostream>

using namespace std;

// [[nodiscard]] will make program throw error is programmer calls func without using return value
[[nodiscard]] vk::raii::ShaderModule WPipeline::CreateShaderModule(const VulkanReferences& ref, const string& path) {
    const vector<char> compiledCode = readFile(path);

    vk::ShaderModuleCreateInfo createInfo{
        .codeSize = compiledCode.size() * sizeof(char),
        .pCode = reinterpret_cast<const uint32_t*>(compiledCode.data()) // compiledCode.data() is char* but we wanna read vector as if it has uint32_t (1 byte to 4 byte is dangerous for alignment but vector is aligned to 4 bytes so we good)
    };

    vk::raii::ShaderModule shaderModule(ref.device, createInfo);

    return shaderModule;
}

void ShaderPipeline::Create(const VulkanReferences& ref, const string& path, const vk::Format* colorFormats, const vk::Format& depthFormat, const vector<ShaderParameter::SParameter>& parameters, bool depthEnabled, bool flipFaces) {
    // Create our shader uniforms/descriptor set layouts
    CreateDescriptorSetLayout(ref, parameters);

    // Do programmable stages; Vertex, Fragment
    // Then fixed-function parameter setup for blending mode, viewport, rasterization

    // PROGRAMMABLE
    auto shaderModule = CreateShaderModule(ref, path);

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = shaderModule,
        .pName = "vertMain" // Name of main func in shader
    };
    // Optional pSpecializationInfo member you can specify values for compile time shader constants (more efficient than uniform)
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = shaderModule,
        .pName = "fragMain" // Name of main func in shader
    };
    array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
        vertShaderStageInfo, fragShaderStageInfo
    };

    // FIXED

    // So much of pipeline state is immutable, baked, but
    // some of it can be changed without recreating pipeline at draw time (viewport size, line width, blend constants)
    vector dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = U32T(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };
    // Choosing to make this dynamic will make us HAVE TO specify it at drawing time and the baked config vals for it will be ignored

    // Format of vertex data = (bindings = spacing, attribute desc = types of attributes)
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,

        .vertexAttributeDescriptionCount = attributeDescriptions.size(),
        .pVertexAttributeDescriptions = attributeDescriptions.data()
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly = {
        .topology = vk::PrimitiveTopology::eTriangleList,
        //.primitiveRestartEnable = true
    };

    // Static examples below, but we're using dynamic so no need to specify
    //// May differ from WIDTH, HEIGHT of window
    //// Viewport defines transformation from image to framebuffer
    //vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height),
    //    0.0f, 1.0f); // Range of depth vals to use for frame buffer

    //// Scissor rectangle discards pixels out of the region
    //// While viewport rectangle stretches image into it
    //vk::Rect2D scissorRect(vk::Offset2D{ 0,0 }, swapChainExtent);

    vk::PipelineViewportStateCreateInfo viewportState = {
        .viewportCount = 1,
        .scissorCount = 1
    };

    // RASTERIZER
    vk::PipelineRasterizationStateCreateInfo rasterizer = {
        .depthClampEnable = vk::False, // frags beyond near far plane are clamped to them instead of discarded, using this requres enabling a gpu feature
        .rasterizerDiscardEnable = vk::False, // disables output to framebuffer
        .polygonMode = vk::PolygonMode::eFill, // could be used for lines or points (requires gpu feature for non fill)
        .cullMode = flipFaces ? vk::CullModeFlagBits::eFront : vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False, // could alter depth vals based on const or frags slope or wtver
        .depthBiasSlopeFactor = 1.0f,
        .lineWidth = 1.0f // > 1 requires gpu feature
    };

    // Disable multisampling (anti-aliasing, but better than rendering to high res => downsampling)
    vk::PipelineMultisampleStateCreateInfo multiSampling = {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False
    };

    // we'll do depth and stencil testing later

    vk::PipelineColorBlendAttachmentState colorBlendAttachment = {
        .blendEnable = vk::True,

        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,

        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp = vk::BlendOp::eAdd,

        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };

    // Pseudo Code below demonstrates blending
    /*if (blendEnable) {
        finalColor.rgb = (srcColorBlendFactor * newColor.rgb) COLORBLEND_OP (dstColorBlendFactor * oldColor.rgb);
        finalColor.a = (srcAlphaBlendFactor * newColor.a) ALPHABLEND_OP (dstAlphaBlendFactor * oldColor.a);
    }
    else {
        finalColor = newColor;
    }
    finalColor = finalColor & colorWriteMask;*/

    vk::PipelineColorBlendStateCreateInfo colorBlending = {
        .logicOpEnable = vk::False, // Alternate method of color blending we won't use since we're using the normal method above (will auto DISABLE FIRST METHOD)
        .logicOp = vk::LogicOp::eCopy, // That method is specifying Blending Logic Here
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment
    };

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {
        .setLayoutCount = 1,
        .pSetLayouts = &*descriptorSetLayout,
        .pushConstantRangeCount = 0 // different way of pushing dynamic vals to shaders
    };
    pipelineLayout = vk::raii::PipelineLayout(ref.device, pipelineLayoutInfo);

    // Depth & stencil state
    vk::PipelineDepthStencilStateCreateInfo depthStencil = {
        .depthTestEnable = depthEnabled,
        .depthWriteEnable = depthEnabled, // should new frags write to depth buff
        .depthCompareOp = depthEnabled ? vk::CompareOp::eLess : vk::CompareOp::eNever, // 1 is far plane 0 is near
        .depthBoundsTestEnable = vk::False, // only keep fragments within specified depth range
        .stencilTestEnable = vk::False // you'd need stencil component in depth/stencil image format
    };

    // Dynamic (simplified) rendering setup
    vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo = {
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = colorFormats,//&swapSurfaceFormat.format,
        .depthAttachmentFormat = depthFormat,//GetDepthFormat(),
    };

    vk::GraphicsPipelineCreateInfo pipelineInfo = {
        .pNext = &pipelineRenderingCreateInfo,
        .stageCount = 2, .pStages = shaderStages.data(),
        .pVertexInputState = &vertexInputInfo, .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState, .pRasterizationState = &rasterizer,
        .pMultisampleState = &multiSampling, .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState, .layout = pipelineLayout,

        .renderPass = nullptr, // dynamic rendering removes need for render pass

        // OPTIONAL, you can make pipelines derive from a similar pipeline to simplify and speedup creation, we're not doing that here so it's optional
        // would also need VK_PIPELINE_CREATE_DERIVATIVE_BIT
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };

    // nullptr is PipelineCache object which stores creation info across multiple calls to create pipeline, speed up pipeline creation significantly
    pipeline = vk::raii::Pipeline(ref.device, nullptr, pipelineInfo);

}

void WPipeline::CreateDescriptorSetLayout(const VulkanReferences& ref, const vector<ShaderParameter::SParameter>& parameters) {
    // Layout of descriptor set (pointers to uniform data)
    vector<vk::DescriptorSetLayoutBinding> bindingLayouts;
    for (int i = 0; i < parameters.size(); i++) {
        vk::DescriptorType dType;
        const auto& param = parameters[i];
        switch (param.type) {
        case ShaderParameter::Type::UNIFORM:
            dType = vk::DescriptorType::eUniformBuffer; break;
        case ShaderParameter::Type::DYNAMIC_UNIFORM:
            dType = vk::DescriptorType::eUniformBufferDynamic; break;
        case ShaderParameter::Type::SAMPLER:
            dType = vk::DescriptorType::eCombinedImageSampler;  break;
        case ShaderParameter::Type::BUFFER:
            dType = vk::DescriptorType::eStorageBuffer; break;
        default:
            assert(false); break;
        }
        bindingLayouts.emplace_back(i, dType, 1, param.visibility);
    }

    vk::DescriptorSetLayoutCreateInfo layoutInfo = {
        .bindingCount = static_cast<uint32_t>(bindingLayouts.size()),
        .pBindings = bindingLayouts.data()
    };
    descriptorSetLayout = vk::raii::DescriptorSetLayout(ref.device, layoutInfo);
}

void ShaderPipeline::Bind(const vk::raii::CommandBuffer& cmd) const {
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
}
void ShaderPipeline::BindDescriptorSets(const vk::raii::CommandBuffer& cmd, const vk::raii::DescriptorSet& descriptorSet) const {
    assert(false); // dont use this method anymore
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, *descriptorSet, nullptr); // change nullptr to offsets
}
void ShaderPipeline::BindMaterialDescriptorSets(const VulkanReferences& ref, const vk::raii::CommandBuffer& cmd, const Material& mat, uint32_t setIndex, vector<uint32_t> dynamicIndices) const {
    vector<uint32_t> dynamicOffsets;
    int i = 0;
    for (const auto& param : mat.params) {
        if (param.type == ShaderParameter::Type::DYNAMIC_UNIFORM) {
            dynamicOffsets.push_back(dynamicIndices[i] * ceilToNearest(param.dynamicUniform.singleObjectSize, ref.minUniformBufferOffsetAlignment));
            ++i;
        }
    }
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, *mat.descriptorSets[setIndex], dynamicOffsets);
}

void ComputePipeline::Create(const VulkanReferences& ref, const string& path, const vector<ShaderParameter::SParameter>& parameters, const vector<ShaderParameter::MParameter>& parameterData, uvec3 workGroupSize, bool usePushConstants, vk::DeviceSize pushConstantSize) {
    this->workGroupSize = workGroupSize;
    this->usePushConstants = usePushConstants;
    this->pushConstantsSize = pushConstantSize;

    //
    auto computeModule = CreateShaderModule(ref, path);

    vk::PipelineShaderStageCreateInfo computeShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = computeModule,
        .pName = "main" // Name of main func in shader
    };

    for (const auto& param : parameters) {
        assert((param.visibility & vk::ShaderStageFlagBits::eCompute) == vk::ShaderStageFlagBits::eCompute);
    }
    assert(descriptorSetLayout == nullptr);
    CreateDescriptorSetLayout(ref, parameters);
    assert(descriptorSetLayout != nullptr);

    assert(!usePushConstants || pushConstantSize != 0);
    vk::PushConstantRange pushConstantRange = {
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = static_cast<uint32_t>(pushConstantSize)
    };

    vk::PipelineLayoutCreateInfo layoutInfo = {
        .setLayoutCount = 1,
        .pSetLayouts = &*descriptorSetLayout,

        .pushConstantRangeCount = usePushConstants ? 1u : 0u,
        .pPushConstantRanges = &pushConstantRange
    };

    pipelineLayout = PipelineLayout(ref.device, layoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo = {
        .stage = computeShaderStageInfo,
        .layout = pipelineLayout
    };

    pipeline = ref.device.createComputePipeline(nullptr, pipelineInfo);

    //
    computeMaterial = mkU<Material>();
    computeMaterial->Create(this, ref, parameterData, 1);

    //
    this->isCreated = true;
}

void ComputePipeline::EnqueueDispatch(ComputeDispatcher* dispatcher, uvec3 totalThreadCount) {
    assert(isCreated);

    dispatcher->cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    dispatcher->cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, *(computeMaterial->descriptorSets[0]), nullptr);

    uvec3 workGroupCount = ceil(vec3(totalThreadCount) / vec3(workGroupSize));
    // std::cout << "wx: " << workGroupCount.x << " wy: " << workGroupCount.y << " wz: " << workGroupCount.z << std::endl;
    dispatcher->cmd.dispatch(workGroupCount.x, workGroupCount.y, workGroupCount.z);
}

void ComputePipeline::EnqueuePushConstants(ComputeDispatcher* dispatcher, void* data) {
    // todo: use modern but requires generics?
    assert(pushConstantsSize != 0);
    vkCmdPushConstants(*dispatcher->cmd, *pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantsSize, data);
}

void ComputePipeline::EnqueueComputeBarrier(ComputeDispatcher* dispatcher, vk::AccessFlags srcAccess, vk::AccessFlags dstAccess) {
    vk::MemoryBarrier barrier = {
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess
    };

    dispatcher->cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eComputeShader,
        {}, barrier, {}, {}
    );
}