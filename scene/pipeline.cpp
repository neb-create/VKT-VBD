#include "pipeline.h"
#include <array>
#include <vector>
#include "scene/resource-helper.h"
#include "scene/mesh.h"

using namespace std;

// TODO: make resource helper
// [[nodiscard]] will make program throw error is programmer calls func without using return value
[[nodiscard]] vk::raii::ShaderModule ShaderPipeline::CreateShaderModule(const VulkanReferences& ref, const string& path) {
    const vector<char> compiledCode = readFile(path);

    vk::ShaderModuleCreateInfo createInfo{
        .codeSize = compiledCode.size() * sizeof(char),
        .pCode = reinterpret_cast<const uint32_t*>(compiledCode.data()) // compiledCode.data() is char* but we wanna read vector as if it has uint32_t (1 byte to 4 byte is dangerous for alignment but vector is aligned to 4 bytes so we good)
    };

    vk::raii::ShaderModule shaderModule(ref.device, createInfo);

    return shaderModule;
}

void ShaderPipeline::Create(const VulkanReferences& ref, const string& path, const vk::Format* colorFormats, const vk::Format& depthFormat) {
    // Create our shader uniforms/descriptor set layouts
    CreateDescriptorSetLayout(ref);

    // Do programmable stages; Vertex, Fragment
    // Then fixed-function parameter setup for blending mode, viewport, rasterization

    // PROGRAMMABLE
    auto shaderModule = CreateShaderModule(ref, "shaders/slang.spv");

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
        .topology = vk::PrimitiveTopology::eTriangleList
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
        .cullMode = vk::CullModeFlagBits::eBack,
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
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True, // should new frags write to depth buff
        .depthCompareOp = vk::CompareOp::eLess, // 1 is far plane 0 is near
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
    graphicsPipeline = vk::raii::Pipeline(ref.device, nullptr, pipelineInfo);

}

// TODO: get that create binding layout system i did in webgpu, cuz rn it's just hardcoded, make it take in shaderparams vecctor
void ShaderPipeline::CreateDescriptorSetLayout(const VulkanReferences& ref) {
    // Layout of descriptor set (sorta pointers to uniforms)
    array bindings = {
        vk::DescriptorSetLayoutBinding(
            0, // Binding index used in shader
            vk::DescriptorType::eUniformBuffer, // Type 
            1, // How many objects?
            vk::ShaderStageFlagBits::eAllGraphics, // Where can we reference 
            nullptr), // Image sampling (later)

        vk::DescriptorSetLayoutBinding(
            1,
            vk::DescriptorType::eCombinedImageSampler,
            1,
            vk::ShaderStageFlagBits::eFragment,
            nullptr),
    };

    vk::DescriptorSetLayoutCreateInfo layoutInfo = {
        .bindingCount = bindings.size(),
        .pBindings = bindings.data()
    };
    descriptorSetLayout = vk::raii::DescriptorSetLayout(ref.device, layoutInfo);
}

void ShaderPipeline::Bind(const vk::raii::CommandBuffer& cmd) {
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
}
void ShaderPipeline::BindDescriptorSets(const vk::raii::CommandBuffer& cmd, const vk::raii::DescriptorSet& descriptorSet) {
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, *descriptorSet, nullptr);
}