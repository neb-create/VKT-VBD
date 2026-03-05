#include "render-pass.h"

void WRenderPass::Create(const VulkanReferences& ref) {
    this->ref = &ref;
    this->drawFence = vk::raii::Fence(ref.device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
}

void WRenderPass::Start(RenderTarget* target, vk::raii::CommandBuffer* cmd) {
    WaitForFinish();
    ref->device.resetFences(*drawFence);

    currCmd = cmd;
    this->target = target;

    currCmd->reset();
    currCmd->begin({}); // could put flags in here

    // Transition to COLOR ATTACHMENT OPTIMAL
    //WTexture::TransitionImageLayout(
    //    *currCmd,
    //    *(target->colorTex->image),
    //    vk::ImageLayout::eUndefined, // From any?
    //    vk::ImageLayout::eColorAttachmentOptimal, // To this format
    //    {}, // What access to wait for?  We don't wanna wait for anything
    //    vk::AccessFlagBits2::eColorAttachmentWrite,
    //    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    //    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    //    vk::ImageAspectFlagBits::eColor, target->colorTex->arrayLayerCount);
    // doing this makes the entire cubemap texture look horrible for some reason, it should be redundant (texture creation already transitions) but idk why it's bad, idk why, maybe ask taaron

    // Transition the depth image to its optimal (from whatever it was we dont care)
    if (target->depthTex) {
        WTexture::TransitionImageLayout(
            *currCmd,
            *target->depthTex->image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::ImageAspectFlagBits::eDepth, 1
        );
    }

    vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo colorAttachmentInfo = {
        .imageView = *target->colorView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear, // what to do before rendering?
        .storeOp = vk::AttachmentStoreOp::eStore, // what to do after rendering?
        .clearValue = clearColor
    };

    vk::RenderingAttachmentInfo depthAttachmentInfo;
    if (target->depthTex) {
        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);
        depthAttachmentInfo = {
            .imageView = *target->depthView,
            .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare, // no need to keep depth
            .clearValue = clearDepth
        };
    }

    vk::RenderingInfo renderingInfo = {
        .renderArea = {.offset = {0,0}, .extent = {target->colorTex->width, target->colorTex->height} },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo, // Which color attachments we rendering to?

        .pDepthAttachment = target->depthTex ? &depthAttachmentInfo : nullptr,
    };

    // All record cmds return void so no error handling til we finished recording
    currCmd->beginRendering(renderingInfo);
    currCmd->setPrimitiveRestartEnable(true); // TODO: check if necessary, we used this for getting prim index

    // Assuming pipeline has viewport and scissor as dynamic render-time specified
    currCmd->setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(target->colorTex->width), static_cast<float>(target->colorTex->height), 0.0f, 1.0f));
    currCmd->setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), { target->colorTex->width, target->colorTex->height }));
}

void WRenderPass::EnqueueSetMaterial(const Material& mat) {
    (static_cast<const ShaderPipeline*>(mat.pipeline))->Bind(*currCmd);
    (static_cast<const ShaderPipeline*>(mat.pipeline))->BindDescriptorSets(*currCmd, mat.descriptorSets[0]); // Assuming doesn't use frames in flight
}

void WRenderPass::EnqueueDraw(const Mesh& mesh) {
    currCmd->bindVertexBuffers(0, *(mesh.vertexBuffer.buffer), { 0 }); // Bind buffer to our binding which has layout and stride stuff {0} is array of vertex buffers to bind
    currCmd->bindIndexBuffer(*(mesh.indexBuffer.buffer), 0, vk::IndexType::eUint32);
    currCmd->drawIndexed(mesh.indexCount, 1, 0, 0, 0);
}

// TODO: also a transition for depth maybe?
void WRenderPass::FinishExecute(vk::ImageLayout targetColorLayout, bool waitForFinish) {
    currCmd->endRendering();
    target->colorTex->TransitionImageLayoutHardcodedEnqueue(currCmd, *ref, vk::ImageLayout::eColorAttachmentOptimal, targetColorLayout);
    currCmd->end();

    vk::PipelineStageFlags waitDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    const vk::SubmitInfo submitInfo = {
            .pWaitDstStageMask = &waitDstStageMask,

            .commandBufferCount = 1,
            .pCommandBuffers = &**currCmd,
    };
    ref->graphicsQueue.submit(submitInfo, *drawFence);

    currCmd = nullptr;
    target = nullptr;

    if (waitForFinish) {
        WaitForFinish();
    }
}

void WRenderPass::WaitForFinish() {
    while (ref->device.waitForFences(*drawFence, vk::True, UINT64_MAX) == vk::Result::eTimeout) {}
}