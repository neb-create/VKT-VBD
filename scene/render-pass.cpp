#include "render-pass.h"
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_vulkan.h"

void WRenderPass::Create(const VulkanReferences& ref) {
    this->ref = &ref;
    this->drawFence = vk::raii::Fence(ref.device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
}

void WRenderPass::Start(RenderTarget* target, vk::raii::CommandBuffer* cmd, bool waitForPrevFinish) {
    if (waitForPrevFinish) {
        WaitForFinish();
    }
    ref->device.resetFences(*drawFence);

    currCmd = cmd;
    this->target = target;

    currCmd->reset();
    currCmd->begin({}); // could put flags in here

    WTexture::TransitionImageLayout(
        *currCmd,
        target->colorImg,
        vk::ImageLayout::eUndefined, // From any?
        vk::ImageLayout::eColorAttachmentOptimal, // To this format
        {}, // What access to wait for?  We don't wanna wait for anything
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::ImageAspectFlagBits::eColor, target->layerCount);
    // doing this makes the entire cubemap texture look horrible for some reason, it should be redundant for cubemap (texture creation already transitions) but idk why it's bad, idk why, maybe ask taaron
    // however, for rendering to swapchain image every frame, it's necessary

    // Transition the depth image to its optimal (from whatever it was we dont care)
    if (target->depthImg) {
        WTexture::TransitionImageLayout(
            *currCmd,
            target->depthImg,
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
        .imageView = target->colorView,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear, // what to do before rendering?
        .storeOp = vk::AttachmentStoreOp::eStore, // what to do after rendering?
        .clearValue = clearColor
    };

    vk::RenderingAttachmentInfo depthAttachmentInfo;
    if (target->depthImg) {
        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);
        depthAttachmentInfo = {
            .imageView = target->depthView,
            .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare, // no need to keep depth
            .clearValue = clearDepth
        };
    }

    vk::RenderingInfo renderingInfo = {
        .renderArea = {.offset = {0,0}, .extent = {target->dim.x, target->dim.y} },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo, // Which color attachments we rendering to?

        .pDepthAttachment = target->depthImg ? &depthAttachmentInfo : nullptr,
    };

    // All record cmds return void so no error handling til we finished recording
    currCmd->beginRendering(renderingInfo);
    currCmd->setPrimitiveRestartEnable(true); // TODO: check if necessary, we used this for getting prim index

    // Assuming pipeline has viewport and scissor as dynamic render-time specified
    currCmd->setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(target->dim.x), static_cast<float>(target->dim.y), 0.0f, 1.0f));
    currCmd->setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), { target->dim.x, target->dim.y }));
}

void WRenderPass::EnqueueSetMaterial(const Material& mat, int setIndex, vector<uint32_t> dynamicIndices) {
    (static_cast<const ShaderPipeline*>(mat.pipeline))->Bind(*currCmd); // TODO: separate binding mat and shader for optimization
    (static_cast<const ShaderPipeline*>(mat.pipeline))->BindMaterialDescriptorSets(*ref, *currCmd, mat, setIndex, dynamicIndices);
}

void WRenderPass::EnqueueDraw(const Mesh& mesh) {
    currCmd->bindVertexBuffers(0, *(mesh.vertexBuffer.buffer), { 0 }); // Bind buffer to our binding which has layout and stride stuff {0} is array of vertex buffers to bind
    currCmd->bindIndexBuffer(*(mesh.indexBuffer.buffer), 0, vk::IndexType::eUint32);
    currCmd->drawIndexed(mesh.indexCount, 1, 0, 0, 0);
}

// TODO: also a transition for depth maybe?
void WRenderPass::FinishExecute(bool waitForFinish, vk::ImageLayout postTargetColorLayout, vk::raii::Semaphore* waitSemaphore, vk::raii::Semaphore* signalSemaphore) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), **currCmd);
    currCmd->endRendering();
    if(postTargetColorLayout != vk::ImageLayout::eUndefined) WTexture::StaticTransitionImageLayoutHardcodedEnqueue(currCmd, *ref, target->colorImg, vk::ImageLayout::eColorAttachmentOptimal, postTargetColorLayout);
    currCmd->end();

    vk::PipelineStageFlags waitDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    const vk::SubmitInfo submitInfo = {
            .waitSemaphoreCount = waitSemaphore == nullptr ? 0u : 1u,
            .pWaitSemaphores = waitSemaphore == nullptr ? nullptr : &**waitSemaphore,
            .pWaitDstStageMask = &waitDstStageMask,

            .commandBufferCount = 1,
            .pCommandBuffers = &**currCmd,

            .signalSemaphoreCount = signalSemaphore == nullptr ? 0u : 1u,
            .pSignalSemaphores = signalSemaphore == nullptr ? nullptr : &**signalSemaphore
    };
    ref->graphicsQueue.submit(submitInfo, *drawFence);

    currCmd = nullptr;
    target = nullptr;

    if (waitForFinish) {
        WaitForFinish();
    }
}

void WRenderPass::WaitForFinish() {
    vk::Result res;
    do {
        res = ref->device.waitForFences(*drawFence, vk::True, UINT64_MAX);
        if (res != vk::Result::eTimeout && res != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait for WRenderPass::WaitForFinish() fence");
        }
    } while (res == vk::Result::eTimeout);
}